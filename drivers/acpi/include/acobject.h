
/******************************************************************************
 *
 * Name: acobject.h - Definition of acpi_operand_object  (Internal object only)
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

#ifndef _ACOBJECT_H
#define _ACOBJECT_H


/*
 * The acpi_operand_object  is used to pass AML operands from the dispatcher
 * to the interpreter, and to keep track of the various handlers such as
 * address space handlers and notify handlers.  The object is a constant
 * size in order to allow it to be cached and reused.
 */

/*******************************************************************************
 *
 * Common Descriptors
 *
 ******************************************************************************/

/*
 * Common area for all objects.
 *
 * Data_type is used to differentiate between internal descriptors, and MUST
 * be the first byte in this structure.
 */
#define ACPI_OBJECT_COMMON_HEADER           /* SIZE/ALIGNMENT: 32 bits, one ptr plus trailing 8-bit flag */\
	u8                          descriptor;         /* To differentiate various internal objs */\
	u8                          type;               /* acpi_object_type */\
	u16                         reference_count;    /* For object deletion management */\
	union acpi_operand_obj      *next_object;       /* Objects linked to parent NS node */\
	u8                          flags; \

/* Values for flag byte above */

#define AOPOBJ_AML_CONSTANT         0x01
#define AOPOBJ_STATIC_POINTER       0x02
#define AOPOBJ_DATA_VALID           0x04
#define AOPOBJ_OBJECT_INITIALIZED   0x08
#define AOPOBJ_SETUP_COMPLETE       0x10
#define AOPOBJ_SINGLE_DATUM         0x20


/*
 * Common bitfield for the field objects
 * "Field Datum"  -- a datum from the actual field object
 * "Buffer Datum" -- a datum from a user buffer, read from or to be written to the field
 */
#define ACPI_COMMON_FIELD_INFO              /* SIZE/ALIGNMENT: 24 bits + three 32-bit values */\
	u8                          field_flags;        /* Access, update, and lock bits */\
	u8                          attribute;          /* From Access_as keyword */\
	u8                          access_byte_width;  /* Read/Write size in bytes */\
	u32                         bit_length;         /* Length of field in bits */\
	u32                         base_byte_offset;   /* Byte offset within containing object */\
	u8                          start_field_bit_offset;/* Bit offset within first field datum (0-63) */\
	u8                          datum_valid_bits;   /* Valid bit in first "Field datum" */\
	u8                          end_field_valid_bits; /* Valid bits in the last "field datum" */\
	u8                          end_buffer_valid_bits; /* Valid bits in the last "buffer datum" */\
	u32                         value;              /* Value to store into the Bank or Index register */\
	acpi_namespace_node         *node;              /* Link back to parent node */


/*
 * Fields common to both Strings and Buffers
 */
#define ACPI_COMMON_BUFFER_INFO \
	u32                         length;


/*
 * Common fields for objects that support ASL notifications
 */
#define ACPI_COMMON_NOTIFY_INFO \
	union acpi_operand_obj      *sys_handler;        /* Handler for system notifies */\
	union acpi_operand_obj      *drv_handler;        /* Handler for driver notifies */\
	union acpi_operand_obj      *addr_handler;       /* Handler for Address space */


/******************************************************************************
 *
 * Basic data types
 *
 *****************************************************************************/

typedef struct acpi_object_common
{
	ACPI_OBJECT_COMMON_HEADER

} acpi_object_common;


typedef struct acpi_object_integer
{
	ACPI_OBJECT_COMMON_HEADER

	acpi_integer                value;

} acpi_object_integer;


typedef struct acpi_object_string                   /* Null terminated, ASCII characters only */
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_BUFFER_INFO
	char                        *pointer;           /* String in AML stream or allocated string */

} acpi_object_string;


typedef struct acpi_object_buffer
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_BUFFER_INFO
	u8                          *pointer;           /* Buffer in AML stream or allocated buffer */
	acpi_namespace_node         *node;              /* Link back to parent node */
	u8                          *aml_start;
	u32                         aml_length;

} acpi_object_buffer;


typedef struct acpi_object_package
{
	ACPI_OBJECT_COMMON_HEADER

	u32                         count;              /* # of elements in package */
	u32                         aml_length;
	u8                          *aml_start;
	acpi_namespace_node         *node;              /* Link back to parent node */
	union acpi_operand_obj      **elements;         /* Array of pointers to Acpi_objects */

} acpi_object_package;


/******************************************************************************
 *
 * Complex data types
 *
 *****************************************************************************/

typedef struct acpi_object_event
{
	ACPI_OBJECT_COMMON_HEADER
	void                        *semaphore;

} acpi_object_event;


#define INFINITE_CONCURRENCY        0xFF

typedef struct acpi_object_method
{
	ACPI_OBJECT_COMMON_HEADER
	u8                          method_flags;
	u8                          param_count;

	u32                         aml_length;

	void                        *semaphore;
	u8                          *aml_start;

	u8                          concurrency;
	u8                          thread_count;
	acpi_owner_id               owning_id;

} acpi_object_method;


typedef struct acpi_object_mutex
{
	ACPI_OBJECT_COMMON_HEADER
	u16                         sync_level;
	u16                         acquisition_depth;

	struct acpi_thread_state    *owner_thread;
	void                        *semaphore;
	union acpi_operand_obj      *prev;              /* Link for list of acquired mutexes */
	union acpi_operand_obj      *next;              /* Link for list of acquired mutexes */
	acpi_namespace_node         *node;              /* containing object */

} acpi_object_mutex;


typedef struct acpi_object_region
{
	ACPI_OBJECT_COMMON_HEADER

	u8                          space_id;

	union acpi_operand_obj      *addr_handler;      /* Handler for system notifies */
	acpi_namespace_node         *node;              /* containing object */
	union acpi_operand_obj      *next;
	u32                         length;
	acpi_physical_address       address;

} acpi_object_region;


/******************************************************************************
 *
 * Objects that can be notified.  All share a common Notify_info area.
 *
 *****************************************************************************/

typedef struct acpi_object_notify_common            /* COMMON NOTIFY for POWER, PROCESSOR, DEVICE, and THERMAL */
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_NOTIFY_INFO

} acpi_object_notify_common;


typedef struct acpi_object_device
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_NOTIFY_INFO

} acpi_object_device;


typedef struct acpi_object_power_resource
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_NOTIFY_INFO

	u32                         system_level;
	u32                         resource_order;

} acpi_object_power_resource;


typedef struct acpi_object_processor
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_NOTIFY_INFO

	u32                         proc_id;
	u32                         length;
	acpi_io_address             address;

} acpi_object_processor;


typedef struct acpi_object_thermal_zone
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_NOTIFY_INFO


} acpi_object_thermal_zone;


/******************************************************************************
 *
 * Fields.  All share a common header/info field.
 *
 *****************************************************************************/

typedef struct acpi_object_field_common             /* COMMON FIELD (for BUFFER, REGION, BANK, and INDEX fields) */
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_FIELD_INFO
	union acpi_operand_obj      *region_obj;        /* Containing Operation Region object */
			 /* (REGION/BANK fields only) */
} acpi_object_field_common;


typedef struct acpi_object_region_field
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_FIELD_INFO
	union acpi_operand_obj      *region_obj;        /* Containing Op_region object */

} acpi_object_region_field;


typedef struct acpi_object_bank_field
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_FIELD_INFO

	union acpi_operand_obj      *region_obj;        /* Containing Op_region object */
	union acpi_operand_obj      *bank_obj;          /* Bank_select Register object */

} acpi_object_bank_field;


typedef struct acpi_object_index_field
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_FIELD_INFO

	/*
	 * No "Region_obj" pointer needed since the Index and Data registers
	 * are each field definitions unto themselves.
	 */
	union acpi_operand_obj      *index_obj;         /* Index register */
	union acpi_operand_obj      *data_obj;          /* Data register */


} acpi_object_index_field;


/* The Buffer_field is different in that it is part of a Buffer, not an Op_region */

typedef struct acpi_object_buffer_field
{
	ACPI_OBJECT_COMMON_HEADER
	ACPI_COMMON_FIELD_INFO

	union acpi_operand_obj      *buffer_obj;        /* Containing Buffer object */

} acpi_object_buffer_field;


/******************************************************************************
 *
 * Objects for handlers
 *
 *****************************************************************************/

typedef struct acpi_object_notify_handler
{
	ACPI_OBJECT_COMMON_HEADER

	acpi_namespace_node         *node;               /* Parent device */
	acpi_notify_handler         handler;
	void                        *context;

} acpi_object_notify_handler;


/* Flags for address handler */

#define ACPI_ADDR_HANDLER_DEFAULT_INSTALLED  0x1


typedef struct acpi_object_addr_handler
{
	ACPI_OBJECT_COMMON_HEADER

	u8                          space_id;
	u16                         hflags;
	acpi_adr_space_handler      handler;

	acpi_namespace_node         *node;              /* Parent device */
	void                        *context;
	acpi_adr_space_setup        setup;
	union acpi_operand_obj      *region_list;       /* regions using this handler */
	union acpi_operand_obj      *next;

} acpi_object_addr_handler;


/******************************************************************************
 *
 * Special internal objects
 *
 *****************************************************************************/

/*
 * The Reference object type is used for these opcodes:
 * Arg[0-6], Local[0-7], Index_op, Name_op, Zero_op, One_op, Ones_op, Debug_op
 */
typedef struct acpi_object_reference
{
	ACPI_OBJECT_COMMON_HEADER

	u8                          target_type;        /* Used for Index_op */
	u16                         opcode;
	u32                         offset;             /* Used for Arg_op, Local_op, and Index_op */

	void                        *object;            /* Name_op=>HANDLE to obj, Index_op=>acpi_operand_object */
	acpi_namespace_node         *node;
	union acpi_operand_obj      **where;

} acpi_object_reference;


/*
 * Extra object is used as additional storage for types that
 * have AML code in their declarations (Term_args) that must be
 * evaluated at run time.
 *
 * Currently: Region and Field_unit types
 */
typedef struct acpi_object_extra
{
	ACPI_OBJECT_COMMON_HEADER
	u8                          byte_fill1;
	u16                         word_fill1;
	u32                         aml_length;
	u8                          *aml_start;
	acpi_namespace_node         *method_REG;        /* _REG method for this region (if any) */
	void                        *region_context;    /* Region-specific data */

} acpi_object_extra;


/* Additional data that can be attached to namespace nodes */

typedef struct acpi_object_data
{
	ACPI_OBJECT_COMMON_HEADER
	acpi_object_handler         handler;
	void                        *pointer;

} acpi_object_data;


/* Structure used when objects are cached for reuse */

typedef struct acpi_object_cache_list
{
	ACPI_OBJECT_COMMON_HEADER
	union acpi_operand_obj      *next;              /* Link for object cache and internal lists*/

} acpi_object_cache_list;


/******************************************************************************
 *
 * acpi_operand_object Descriptor - a giant union of all of the above
 *
 *****************************************************************************/

typedef union acpi_operand_obj
{
	acpi_object_common          common;

	acpi_object_integer         integer;
	acpi_object_string          string;
	acpi_object_buffer          buffer;
	acpi_object_package         package;

	acpi_object_event           event;
	acpi_object_method          method;
	acpi_object_mutex           mutex;
	acpi_object_region          region;

	acpi_object_notify_common   common_notify;
	acpi_object_device          device;
	acpi_object_power_resource  power_resource;
	acpi_object_processor       processor;
	acpi_object_thermal_zone    thermal_zone;

	acpi_object_field_common    common_field;
	acpi_object_region_field    field;
	acpi_object_buffer_field    buffer_field;
	acpi_object_bank_field      bank_field;
	acpi_object_index_field     index_field;

	acpi_object_notify_handler  notify_handler;
	acpi_object_addr_handler    addr_handler;

	acpi_object_reference       reference;
	acpi_object_extra           extra;
	acpi_object_data            data;
	acpi_object_cache_list      cache;

} acpi_operand_object;


/******************************************************************************
 *
 * acpi_descriptor - objects that share a common descriptor identifier
 *
 *****************************************************************************/


/* Object descriptor types */

#define ACPI_DESC_TYPE_CACHED           0x11    /* Used only when object is cached */
#define ACPI_DESC_TYPE_STATE            0x20
#define ACPI_DESC_TYPE_STATE_UPDATE     0x21
#define ACPI_DESC_TYPE_STATE_PACKAGE    0x22
#define ACPI_DESC_TYPE_STATE_CONTROL    0x23
#define ACPI_DESC_TYPE_STATE_RPSCOPE    0x24
#define ACPI_DESC_TYPE_STATE_PSCOPE     0x25
#define ACPI_DESC_TYPE_STATE_WSCOPE     0x26
#define ACPI_DESC_TYPE_STATE_RESULT     0x27
#define ACPI_DESC_TYPE_STATE_NOTIFY     0x28
#define ACPI_DESC_TYPE_STATE_THREAD     0x29
#define ACPI_DESC_TYPE_WALK             0x44
#define ACPI_DESC_TYPE_PARSER           0x66
#define ACPI_DESC_TYPE_OPERAND          0x88
#define ACPI_DESC_TYPE_NAMED            0xAA


typedef union acpi_desc
{
	u8                          descriptor_id;        /* To differentiate various internal objs */\
	acpi_operand_object         object;
	acpi_namespace_node         node;
	acpi_parse_object           op;

} acpi_descriptor;


#endif /* _ACOBJECT_H */
