/******************************************************************************
 *
 * Module Name: exdump - Interpreter debug output routines
 *              $Revision: 126 $
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
#include "amlcode.h"
#include "acnamesp.h"
#include "actables.h"
#include "acparser.h"

#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exdump")


/*
 * The following routines are used for debug output only
 */

#if defined(ACPI_DEBUG) || defined(ENABLE_DEBUGGER)

/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_show_hex_value
 *
 * PARAMETERS:  Byte_count          - Number of bytes to print (1, 2, or 4)
 *              *Aml_start            - Address in AML stream of bytes to print
 *              Interpreter_mode    - Current running mode (load1/Load2/Exec)
 *              Lead_space          - # of spaces to print ahead of value
 *                                    0 => none ahead but one behind
 *
 * DESCRIPTION: Print Byte_count byte(s) starting at Aml_start as a single
 *              value, in hex.  If Byte_count > 1 or the value printed is > 9, also
 *              print in decimal.
 *
 ****************************************************************************/

void
acpi_ex_show_hex_value (
	u32                     byte_count,
	u8                      *aml_start,
	u32                     lead_space)
{
	u32                     value;                  /*  Value retrieved from AML stream */
	u32                     show_decimal_value;
	u32                     length;                 /*  Length of printed field */
	u8                      *current_aml_ptr = NULL; /* Pointer to current byte of AML value    */


	FUNCTION_TRACE ("Ex_show_hex_value");


	if (!aml_start) {
		REPORT_ERROR (("Ex_show_hex_value: null pointer\n"));
	}

	/*
	 * AML numbers are always stored little-endian,
	 * even if the processor is big-endian.
	 */
	for (current_aml_ptr = aml_start + byte_count,
			value = 0;
			current_aml_ptr > aml_start; ) {
		value = (value << 8) + (u32)* --current_aml_ptr;
	}

	length = lead_space * byte_count + 2;
	if (byte_count > 1) {
		length += (byte_count - 1);
	}

	show_decimal_value = (byte_count > 1 || value > 9);
	if (show_decimal_value) {
		length += 3 + acpi_ex_digits_needed (value, 10);
	}

	for (length = lead_space; length; --length ) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_LOAD, " "));
	}

	while (byte_count--) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_LOAD, "%02x", *aml_start++));

		if (byte_count) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_LOAD, " "));
		}
	}

	if (show_decimal_value) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_LOAD, " [%d]", value));
	}

	if (0 == lead_space) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_LOAD, " "));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_LOAD, "\n"));
	return_VOID;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_dump_operand
 *
 * PARAMETERS:  *Entry_desc         - Pointer to entry to be dumped
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dump a stack entry
 *
 ****************************************************************************/

acpi_status
acpi_ex_dump_operand (
	acpi_operand_object     *entry_desc)
{
	u8                      *buf = NULL;
	u32                     length;
	u32                     i;


	PROC_NAME ("Ex_dump_operand")


	if (!entry_desc) {
		/*
		 * This usually indicates that something serious is wrong --
		 * since most (if not all)
		 * code that dumps the stack expects something to be there!
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Null stack entry ptr\n"));
		return (AE_OK);
	}

	if (VALID_DESCRIPTOR_TYPE (entry_desc, ACPI_DESC_TYPE_NAMED)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%p NS Node: ", entry_desc));
		DUMP_ENTRY (entry_desc, ACPI_LV_INFO);
		return (AE_OK);
	}

	if (!VALID_DESCRIPTOR_TYPE (entry_desc, ACPI_DESC_TYPE_INTERNAL)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%p Is not a local object \n", entry_desc));
		DUMP_BUFFER (entry_desc, sizeof (acpi_operand_object));
		return (AE_OK);
	}

	/*  Entry_desc is a valid object */

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%p ", entry_desc));

	switch (entry_desc->common.type) {
	case INTERNAL_TYPE_REFERENCE:

		switch (entry_desc->reference.opcode) {
		case AML_ZERO_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: Zero\n"));
			break;


		case AML_ONE_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: One\n"));
			break;


		case AML_ONES_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: Ones\n"));
			break;


		case AML_REVISION_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: Revision\n"));
			break;


		case AML_DEBUG_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: Debug\n"));
			break;


		case AML_NAME_OP:

			DUMP_PATHNAME (entry_desc->reference.object, "Reference: Name: ",
					  ACPI_LV_INFO, _COMPONENT);
			DUMP_ENTRY (entry_desc->reference.object, ACPI_LV_INFO);
			break;


		case AML_INDEX_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: Index %p\n",
					 entry_desc->reference.object));
			break;


		case AML_ARG_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: Arg%d",
					 entry_desc->reference.offset));

			if (ACPI_TYPE_INTEGER == entry_desc->common.type) {
				/* Value is a Number */

				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, " value is [%8.8X%8.8x]",
						  HIDWORD(entry_desc->integer.value),
						  LODWORD(entry_desc->integer.value)));
			}

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\n"));
			break;


		case AML_LOCAL_OP:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference: Local%d",
					 entry_desc->reference.offset));

			if (ACPI_TYPE_INTEGER == entry_desc->common.type) {

				/* Value is a Number */

				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, " value is [%8.8X%8.8x]",
						  HIDWORD(entry_desc->integer.value),
						  LODWORD(entry_desc->integer.value)));
			}

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\n"));
			break;


		case AML_INT_NAMEPATH_OP:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Reference.Node->Name %X\n",
					 entry_desc->reference.node->name));
			break;

		default:

			/*  unknown opcode  */

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Unknown opcode=%X\n",
				entry_desc->reference.opcode));
			break;

		}

		break;


	case ACPI_TYPE_BUFFER:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Buffer len %X @ %p \n",
				 entry_desc->buffer.length,
				 entry_desc->buffer.pointer));

		length = entry_desc->buffer.length;

		if (length > 64) {
			length = 64;
		}

		/* Debug only -- dump the buffer contents */

		if (entry_desc->buffer.pointer) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Buffer Contents: "));

			for (buf = entry_desc->buffer.pointer; length--; ++buf) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, " %02x", *buf));
			}
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO,"\n"));
		}

		break;


	case ACPI_TYPE_INTEGER:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Integer %8.8X%8.8X\n",
				 HIDWORD (entry_desc->integer.value),
				 LODWORD (entry_desc->integer.value)));
		break;


	case INTERNAL_TYPE_IF:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "If [Integer] %8.8X%8.8X\n",
				 HIDWORD (entry_desc->integer.value),
				 LODWORD (entry_desc->integer.value)));
		break;


	case INTERNAL_TYPE_WHILE:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "While [Integer] %8.8X%8.8X\n",
				 HIDWORD (entry_desc->integer.value),
				 LODWORD (entry_desc->integer.value)));
		break;


	case ACPI_TYPE_PACKAGE:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Package count %X @ %p\n",
				 entry_desc->package.count, entry_desc->package.elements));

		/*
		 * If elements exist, package vector pointer is valid,
		 * and debug_level exceeds 1, dump package's elements.
		 */
		if (entry_desc->package.count &&
			entry_desc->package.elements &&
			acpi_dbg_level > 1) {
			acpi_operand_object**element;
			u16                 element_index;

			for (element_index = 0, element = entry_desc->package.elements;
				  element_index < entry_desc->package.count;
				  ++element_index, ++element) {
				acpi_ex_dump_operand (*element);
			}
		}

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\n"));

		break;


	case ACPI_TYPE_REGION:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Region %s (%X)",
			acpi_ut_get_region_name (entry_desc->region.space_id),
			entry_desc->region.space_id));

		/*
		 * If the address and length have not been evaluated,
		 * don't print them.
		 */
		if (!(entry_desc->region.flags & AOPOBJ_DATA_VALID)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\n"));
		}
		else {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, " base %8.8X%8.8X Length %X\n",
				HIDWORD(entry_desc->region.address),
				LODWORD(entry_desc->region.address),
				entry_desc->region.length));
		}
		break;


	case ACPI_TYPE_STRING:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "String length %X @ %p \"",
				 entry_desc->string.length, entry_desc->string.pointer));

		for (i = 0; i < entry_desc->string.length; i++) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "%c",
					 entry_desc->string.pointer[i]));
		}

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\"\n"));
		break;


	case INTERNAL_TYPE_BANK_FIELD:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Bank_field\n"));
		break;


	case INTERNAL_TYPE_REGION_FIELD:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO,
			"Region_field: bits=%X bitaccwidth=%X lock=%X update=%X at byte=%X bit=%X of below:\n",
			entry_desc->field.bit_length,    entry_desc->field.access_bit_width,
			entry_desc->field.lock_rule,     entry_desc->field.update_rule,
			entry_desc->field.base_byte_offset, entry_desc->field.start_field_bit_offset));
		DUMP_STACK_ENTRY (entry_desc->field.region_obj);
		break;


	case INTERNAL_TYPE_INDEX_FIELD:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Index_field\n"));
		break;


	case ACPI_TYPE_BUFFER_FIELD:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO,
			"Buffer_field: %X bits at byte %X bit %X of \n",
			entry_desc->buffer_field.bit_length, entry_desc->buffer_field.base_byte_offset,
			entry_desc->buffer_field.start_field_bit_offset));

		if (!entry_desc->buffer_field.buffer_obj)
		{
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "*NULL* \n"));
		}

		else if (ACPI_TYPE_BUFFER !=
				  entry_desc->buffer_field.buffer_obj->common.type)
		{
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "*not a Buffer* \n"));
		}

		else
		{
			DUMP_STACK_ENTRY (entry_desc->buffer_field.buffer_obj);
		}

		break;


	case ACPI_TYPE_EVENT:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Event\n"));
		break;


	case ACPI_TYPE_METHOD:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO,
			"Method(%X) @ %p:%X\n",
			entry_desc->method.param_count,
			entry_desc->method.aml_start, entry_desc->method.aml_length));
		break;


	case ACPI_TYPE_MUTEX:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Mutex\n"));
		break;


	case ACPI_TYPE_DEVICE:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Device\n"));
		break;


	case ACPI_TYPE_POWER:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Power\n"));
		break;


	case ACPI_TYPE_PROCESSOR:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Processor\n"));
		break;


	case ACPI_TYPE_THERMAL:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Thermal\n"));
		break;


	default:
		/*  unknown Entry_desc->Common.Type value   */

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "Unknown Type %X\n",
			entry_desc->common.type));

		/* Back up to previous entry */

		entry_desc--;


		/* TBD: [Restructure]  Change to use dump object routine !! */
		/*       What is all of this?? */

		DUMP_BUFFER (entry_desc, sizeof (acpi_operand_object));
		DUMP_BUFFER (++entry_desc, sizeof (acpi_operand_object));
		DUMP_BUFFER (++entry_desc, sizeof (acpi_operand_object));
		break;

	}

	return (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_dump_operands
 *
 * PARAMETERS:  Interpreter_mode     - Load or Exec
 *              *Ident              - Identification
 *              Num_levels          - # of stack entries to dump above line
 *              *Note               - Output notation
 *
 * DESCRIPTION: Dump the object stack
 *
 ****************************************************************************/

void
acpi_ex_dump_operands (
	acpi_operand_object     **operands,
	operating_mode          interpreter_mode,
	NATIVE_CHAR             *ident,
	u32                     num_levels,
	NATIVE_CHAR             *note,
	NATIVE_CHAR             *module_name,
	u32                     line_number)
{
	NATIVE_UINT             i;
	acpi_operand_object     **entry_desc;


	PROC_NAME ("Ex_dump_operands");


	if (!ident)
	{
		ident = "?";
	}

	if (!note)
	{
		note = "?";
	}


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"************* Operand Stack Contents (Opcode [%s], %d Operands)\n",
		ident, num_levels));

	if (num_levels == 0)
	{
		num_levels = 1;
	}

	/* Dump the stack starting at the top, working down */

	for (i = 0; num_levels > 0; i--, num_levels--)
	{
		entry_desc = &operands[i];

		if (ACPI_FAILURE (acpi_ex_dump_operand (*entry_desc)))
		{
			break;
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"************* Stack dump from %s(%d), %s\n",
		module_name, line_number, note));
	return;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_dump_node
 *
 * PARAMETERS:  *Node           - Descriptor to dump
 *              Flags               - Force display
 *
 * DESCRIPTION: Dumps the members of the given.Node
 *
 ****************************************************************************/

void
acpi_ex_dump_node (
	acpi_namespace_node     *node,
	u32                     flags)
{

	FUNCTION_ENTRY ();


	if (!flags)
	{
		if (!((ACPI_LV_OBJECTS & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer)))
		{
			return;
		}
	}


	acpi_os_printf ("%20s : %4.4s\n", "Name",           (char*)&node->name);
	acpi_os_printf ("%20s : %s\n",  "Type",             acpi_ut_get_type_name (node->type));
	acpi_os_printf ("%20s : %X\n",  "Flags",            node->flags);
	acpi_os_printf ("%20s : %X\n",  "Owner Id",         node->owner_id);
	acpi_os_printf ("%20s : %X\n",  "Reference Count",  node->reference_count);
	acpi_os_printf ("%20s : %p\n",  "Attached Object",  node->object);
	acpi_os_printf ("%20s : %p\n",  "Child_list",       node->child);
	acpi_os_printf ("%20s : %p\n",  "Next_peer",        node->peer);
	acpi_os_printf ("%20s : %p\n",  "Parent",           acpi_ns_get_parent_object (node));
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_dump_object_descriptor
 *
 * PARAMETERS:  *Object             - Descriptor to dump
 *              Flags               - Force display
 *
 * DESCRIPTION: Dumps the members of the object descriptor given.
 *
 ****************************************************************************/

void
acpi_ex_dump_object_descriptor (
	acpi_operand_object     *obj_desc,
	u32                     flags)
{
	const acpi_opcode_info  *op_info;


	FUNCTION_TRACE ("Ex_dump_object_descriptor");


	if (!flags)
	{
		if (!((ACPI_LV_OBJECTS & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer)))
		{
			return;
		}
	}

	if (!(VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_INTERNAL)))
	{
		acpi_os_printf ("%p is not a valid ACPI object\n", obj_desc);
		return;
	}

	/* Common Fields */

	acpi_os_printf ("%20s : %X\n", "Reference Count", obj_desc->common.reference_count);
	acpi_os_printf ("%20s : %X\n", "Flags", obj_desc->common.flags);

	/* Object-specific Fields */

	switch (obj_desc->common.type)
	{
	case ACPI_TYPE_INTEGER:

		acpi_os_printf ("%20s : %s\n", "Type", "Integer");
		acpi_os_printf ("%20s : %X%8.8X\n", "Value", HIDWORD (obj_desc->integer.value),
				  LODWORD (obj_desc->integer.value));
		break;


	case ACPI_TYPE_STRING:

		acpi_os_printf ("%20s : %s\n", "Type", "String");
		acpi_os_printf ("%20s : %X\n", "Length", obj_desc->string.length);
		acpi_os_printf ("%20s : %p\n", "Pointer", obj_desc->string.pointer);
		break;


	case ACPI_TYPE_BUFFER:

		acpi_os_printf ("%20s : %s\n", "Type", "Buffer");
		acpi_os_printf ("%20s : %X\n", "Length", obj_desc->buffer.length);
		acpi_os_printf ("%20s : %p\n", "Pointer", obj_desc->buffer.pointer);
		break;


	case ACPI_TYPE_PACKAGE:

		acpi_os_printf ("%20s : %s\n", "Type", "Package");
		acpi_os_printf ("%20s : %X\n", "Flags", obj_desc->package.flags);
		acpi_os_printf ("%20s : %X\n", "Count", obj_desc->package.count);
		acpi_os_printf ("%20s : %p\n", "Elements", obj_desc->package.elements);
		acpi_os_printf ("%20s : %p\n", "Next_element", obj_desc->package.next_element);
		break;


	case ACPI_TYPE_BUFFER_FIELD:

		acpi_os_printf ("%20s : %s\n", "Type", "Buffer_field");
		acpi_os_printf ("%20s : %X\n", "Bit_length", obj_desc->buffer_field.bit_length);
		acpi_os_printf ("%20s : %X\n", "Bit_offset", obj_desc->buffer_field.start_field_bit_offset);
		acpi_os_printf ("%20s : %X\n", "Base_byte_offset",obj_desc->buffer_field.base_byte_offset);
		acpi_os_printf ("%20s : %p\n", "Buffer_obj", obj_desc->buffer_field.buffer_obj);
		break;


	case ACPI_TYPE_DEVICE:

		acpi_os_printf ("%20s : %s\n", "Type", "Device");
		acpi_os_printf ("%20s : %p\n", "Addr_handler", obj_desc->device.addr_handler);
		acpi_os_printf ("%20s : %p\n", "Sys_handler", obj_desc->device.sys_handler);
		acpi_os_printf ("%20s : %p\n", "Drv_handler", obj_desc->device.drv_handler);
		break;

	case ACPI_TYPE_EVENT:

		acpi_os_printf ("%20s : %s\n", "Type", "Event");
		acpi_os_printf ("%20s : %X\n", "Semaphore", obj_desc->event.semaphore);
		break;


	case ACPI_TYPE_METHOD:

		acpi_os_printf ("%20s : %s\n", "Type", "Method");
		acpi_os_printf ("%20s : %X\n", "Param_count", obj_desc->method.param_count);
		acpi_os_printf ("%20s : %X\n", "Concurrency", obj_desc->method.concurrency);
		acpi_os_printf ("%20s : %p\n", "Semaphore", obj_desc->method.semaphore);
		acpi_os_printf ("%20s : %X\n", "Aml_length", obj_desc->method.aml_length);
		acpi_os_printf ("%20s : %X\n", "Aml_start", obj_desc->method.aml_start);
		break;


	case ACPI_TYPE_MUTEX:

		acpi_os_printf ("%20s : %s\n", "Type", "Mutex");
		acpi_os_printf ("%20s : %X\n", "Sync_level", obj_desc->mutex.sync_level);
		acpi_os_printf ("%20s : %p\n", "Owner", obj_desc->mutex.owner);
		acpi_os_printf ("%20s : %X\n", "Acquisition_depth", obj_desc->mutex.acquisition_depth);
		acpi_os_printf ("%20s : %p\n", "Semaphore", obj_desc->mutex.semaphore);
		break;


	case ACPI_TYPE_REGION:

		acpi_os_printf ("%20s : %s\n", "Type", "Region");
		acpi_os_printf ("%20s : %X\n", "Space_id", obj_desc->region.space_id);
		acpi_os_printf ("%20s : %X\n", "Flags", obj_desc->region.flags);
		acpi_os_printf ("%20s : %X\n", "Address", obj_desc->region.address);
		acpi_os_printf ("%20s : %X\n", "Length", obj_desc->region.length);
		acpi_os_printf ("%20s : %p\n", "Addr_handler", obj_desc->region.addr_handler);
		acpi_os_printf ("%20s : %p\n", "Next", obj_desc->region.next);
		break;


	case ACPI_TYPE_POWER:

		acpi_os_printf ("%20s : %s\n", "Type", "Power_resource");
		acpi_os_printf ("%20s : %X\n", "System_level", obj_desc->power_resource.system_level);
		acpi_os_printf ("%20s : %X\n", "Resource_order", obj_desc->power_resource.resource_order);
		acpi_os_printf ("%20s : %p\n", "Sys_handler", obj_desc->power_resource.sys_handler);
		acpi_os_printf ("%20s : %p\n", "Drv_handler", obj_desc->power_resource.drv_handler);
		break;


	case ACPI_TYPE_PROCESSOR:

		acpi_os_printf ("%20s : %s\n", "Type", "Processor");
		acpi_os_printf ("%20s : %X\n", "Processor ID", obj_desc->processor.proc_id);
		acpi_os_printf ("%20s : %X\n", "Length", obj_desc->processor.length);
		acpi_os_printf ("%20s : %X\n", "Address", obj_desc->processor.address);
		acpi_os_printf ("%20s : %p\n", "Sys_handler", obj_desc->processor.sys_handler);
		acpi_os_printf ("%20s : %p\n", "Drv_handler", obj_desc->processor.drv_handler);
		acpi_os_printf ("%20s : %p\n", "Addr_handler", obj_desc->processor.addr_handler);
		break;


	case ACPI_TYPE_THERMAL:

		acpi_os_printf ("%20s : %s\n", "Type", "Thermal_zone");
		acpi_os_printf ("%20s : %p\n", "Sys_handler", obj_desc->thermal_zone.sys_handler);
		acpi_os_printf ("%20s : %p\n", "Drv_handler", obj_desc->thermal_zone.drv_handler);
		acpi_os_printf ("%20s : %p\n", "Addr_handler", obj_desc->thermal_zone.addr_handler);
		break;


	case INTERNAL_TYPE_REGION_FIELD:

		acpi_os_printf ("%20s : %p\n", "Access_bit_width", obj_desc->field.access_bit_width);
		acpi_os_printf ("%20s : %p\n", "Bit_length", obj_desc->field.bit_length);
		acpi_os_printf ("%20s : %p\n", "Base_byte_offset",obj_desc->field.base_byte_offset);
		acpi_os_printf ("%20s : %p\n", "Bit_offset", obj_desc->field.start_field_bit_offset);
		acpi_os_printf ("%20s : %p\n", "Region_obj", obj_desc->field.region_obj);
		break;


	case INTERNAL_TYPE_BANK_FIELD:

		acpi_os_printf ("%20s : %s\n", "Type", "Bank_field");
		acpi_os_printf ("%20s : %X\n", "Access_bit_width", obj_desc->bank_field.access_bit_width);
		acpi_os_printf ("%20s : %X\n", "Lock_rule", obj_desc->bank_field.lock_rule);
		acpi_os_printf ("%20s : %X\n", "Update_rule", obj_desc->bank_field.update_rule);
		acpi_os_printf ("%20s : %X\n", "Bit_length", obj_desc->bank_field.bit_length);
		acpi_os_printf ("%20s : %X\n", "Bit_offset", obj_desc->bank_field.start_field_bit_offset);
		acpi_os_printf ("%20s : %X\n", "Base_byte_offset", obj_desc->bank_field.base_byte_offset);
		acpi_os_printf ("%20s : %X\n", "Value", obj_desc->bank_field.value);
		acpi_os_printf ("%20s : %p\n", "Region_obj", obj_desc->bank_field.region_obj);
		acpi_os_printf ("%20s : %X\n", "Bank_register_obj", obj_desc->bank_field.bank_register_obj);
		break;


	case INTERNAL_TYPE_INDEX_FIELD:

		acpi_os_printf ("%20s : %s\n", "Type", "Index_field");
		acpi_os_printf ("%20s : %X\n", "Access_bit_width", obj_desc->index_field.access_bit_width);
		acpi_os_printf ("%20s : %X\n", "Lock_rule", obj_desc->index_field.lock_rule);
		acpi_os_printf ("%20s : %X\n", "Update_rule", obj_desc->index_field.update_rule);
		acpi_os_printf ("%20s : %X\n", "Bit_length", obj_desc->index_field.bit_length);
		acpi_os_printf ("%20s : %X\n", "Bit_offset", obj_desc->index_field.start_field_bit_offset);
		acpi_os_printf ("%20s : %X\n", "Value", obj_desc->index_field.value);
		acpi_os_printf ("%20s : %X\n", "Index", obj_desc->index_field.index_obj);
		acpi_os_printf ("%20s : %X\n", "Data", obj_desc->index_field.data_obj);
		break;


	case INTERNAL_TYPE_REFERENCE:

		op_info = acpi_ps_get_opcode_info (obj_desc->reference.opcode);

		acpi_os_printf ("%20s : %s\n", "Type", "Reference");
		acpi_os_printf ("%20s : %X\n", "Target_type", obj_desc->reference.target_type);
		acpi_os_printf ("%20s : %s\n", "Opcode", op_info->name);
		acpi_os_printf ("%20s : %X\n", "Offset", obj_desc->reference.offset);
		acpi_os_printf ("%20s : %p\n", "Obj_desc", obj_desc->reference.object);
		acpi_os_printf ("%20s : %p\n", "Node", obj_desc->reference.node);
		acpi_os_printf ("%20s : %p\n", "Where", obj_desc->reference.where);
		break;


	case INTERNAL_TYPE_ADDRESS_HANDLER:

		acpi_os_printf ("%20s : %s\n", "Type", "Address Handler");
		acpi_os_printf ("%20s : %X\n", "Space_id", obj_desc->addr_handler.space_id);
		acpi_os_printf ("%20s : %p\n", "Next", obj_desc->addr_handler.next);
		acpi_os_printf ("%20s : %p\n", "Region_list", obj_desc->addr_handler.region_list);
		acpi_os_printf ("%20s : %p\n", "Node", obj_desc->addr_handler.node);
		acpi_os_printf ("%20s : %p\n", "Handler", obj_desc->addr_handler.handler);
		acpi_os_printf ("%20s : %p\n", "Context", obj_desc->addr_handler.context);
		break;


	case INTERNAL_TYPE_NOTIFY:

		acpi_os_printf ("%20s : %s\n", "Type", "Notify Handler");
		acpi_os_printf ("%20s : %p\n", "Node", obj_desc->notify_handler.node);
		acpi_os_printf ("%20s : %p\n", "Handler", obj_desc->notify_handler.handler);
		acpi_os_printf ("%20s : %p\n", "Context", obj_desc->notify_handler.context);
		break;


	case INTERNAL_TYPE_ALIAS:
	case INTERNAL_TYPE_FIELD_DEFN:
	case INTERNAL_TYPE_BANK_FIELD_DEFN:
	case INTERNAL_TYPE_INDEX_FIELD_DEFN:
	case INTERNAL_TYPE_IF:
	case INTERNAL_TYPE_ELSE:
	case INTERNAL_TYPE_WHILE:
	case INTERNAL_TYPE_SCOPE:
	case INTERNAL_TYPE_DEF_ANY:

		acpi_os_printf ("*** Structure display not implemented for type %X! ***\n",
			obj_desc->common.type);
		break;


	default:

		acpi_os_printf ("*** Cannot display unknown type %X! ***\n", obj_desc->common.type);
		break;
	}

	return_VOID;
}

#endif

