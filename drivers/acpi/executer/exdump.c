/******************************************************************************
 *
 * Module Name: exdump - Interpreter debug output routines
 *              $Revision: 159 $
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
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acparser.h"

#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exdump")


/*
 * The following routines are used for debug output only
 */

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)

/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_dump_operand
 *
 * PARAMETERS:  *Obj_desc         - Pointer to entry to be dumped
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dump an operand object
 *
 ****************************************************************************/

void
acpi_ex_dump_operand (
	acpi_operand_object     *obj_desc)
{
	u8                      *buf = NULL;
	u32                     length;
	u32                     i;
	acpi_operand_object     **element;
	u16                     element_index;


	ACPI_FUNCTION_NAME ("Ex_dump_operand")


	if (!((ACPI_LV_EXEC & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer))) {
		return;
	}

	if (!obj_desc) {
		/*
		 * This usually indicates that something serious is wrong --
		 * since most (if not all)
		 * code that dumps the stack expects something to be there!
		 */
		acpi_os_printf ("Null stack entry ptr\n");
		return;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) == ACPI_DESC_TYPE_NAMED) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%p NS Node: ", obj_desc));
		ACPI_DUMP_ENTRY (obj_desc, ACPI_LV_EXEC);
		return;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) != ACPI_DESC_TYPE_OPERAND) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%p is not a local object\n", obj_desc));
		ACPI_DUMP_BUFFER (obj_desc, sizeof (acpi_operand_object));
		return;
	}

	/*  Obj_desc is a valid object */

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%p ", obj_desc));

	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case INTERNAL_TYPE_REFERENCE:

		switch (obj_desc->reference.opcode) {
		case AML_DEBUG_OP:

			acpi_os_printf ("Reference: Debug\n");
			break;


		case AML_NAME_OP:

			ACPI_DUMP_PATHNAME (obj_desc->reference.object, "Reference: Name: ",
					  ACPI_LV_INFO, _COMPONENT);
			ACPI_DUMP_ENTRY (obj_desc->reference.object, ACPI_LV_INFO);
			break;


		case AML_INDEX_OP:

			acpi_os_printf ("Reference: Index %p\n",
					 obj_desc->reference.object);
			break;


		case AML_REF_OF_OP:

			acpi_os_printf ("Reference: (Ref_of) %p\n",
					 obj_desc->reference.object);
			break;


		case AML_ARG_OP:

			acpi_os_printf ("Reference: Arg%d",
					 obj_desc->reference.offset);

			if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_INTEGER) {
				/* Value is a Number */

				acpi_os_printf (" value is [%8.8X%8.8x]",
						 ACPI_HIDWORD(obj_desc->integer.value),
						 ACPI_LODWORD(obj_desc->integer.value));
			}

			acpi_os_printf ("\n");
			break;


		case AML_LOCAL_OP:

			acpi_os_printf ("Reference: Local%d",
					 obj_desc->reference.offset);

			if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_INTEGER) {

				/* Value is a Number */

				acpi_os_printf (" value is [%8.8X%8.8x]",
						 ACPI_HIDWORD(obj_desc->integer.value),
						 ACPI_LODWORD(obj_desc->integer.value));
			}

			acpi_os_printf ("\n");
			break;


		case AML_INT_NAMEPATH_OP:

			acpi_os_printf ("Reference.Node->Name %X\n",
					 obj_desc->reference.node->name.integer);
			break;


		default:

			/*  unknown opcode  */

			acpi_os_printf ("Unknown Reference opcode=%X\n",
				obj_desc->reference.opcode);
			break;

		}

		break;


	case ACPI_TYPE_BUFFER:

		acpi_os_printf ("Buffer len %X @ %p \n",
				 obj_desc->buffer.length,
				 obj_desc->buffer.pointer);

		length = obj_desc->buffer.length;

		if (length > 64) {
			length = 64;
		}

		/* Debug only -- dump the buffer contents */

		if (obj_desc->buffer.pointer) {
			acpi_os_printf ("Buffer Contents: ");

			for (buf = obj_desc->buffer.pointer; length--; ++buf) {
				acpi_os_printf (" %02x", *buf);
			}
			acpi_os_printf ("\n");
		}

		break;


	case ACPI_TYPE_INTEGER:

		acpi_os_printf ("Integer %8.8X%8.8X\n",
				 ACPI_HIDWORD (obj_desc->integer.value),
				 ACPI_LODWORD (obj_desc->integer.value));
		break;


	case INTERNAL_TYPE_IF:

		acpi_os_printf ("If [Integer] %8.8X%8.8X\n",
				 ACPI_HIDWORD (obj_desc->integer.value),
				 ACPI_LODWORD (obj_desc->integer.value));
		break;


	case INTERNAL_TYPE_WHILE:

		acpi_os_printf ("While [Integer] %8.8X%8.8X\n",
				 ACPI_HIDWORD (obj_desc->integer.value),
				 ACPI_LODWORD (obj_desc->integer.value));
		break;


	case ACPI_TYPE_PACKAGE:

		acpi_os_printf ("Package count %X @ %p\n",
				 obj_desc->package.count, obj_desc->package.elements);

		/*
		 * If elements exist, package vector pointer is valid,
		 * and debug_level exceeds 1, dump package's elements.
		 */
		if (obj_desc->package.count &&
			obj_desc->package.elements &&
			acpi_dbg_level > 1) {
			for (element_index = 0, element = obj_desc->package.elements;
				  element_index < obj_desc->package.count;
				  ++element_index, ++element) {
				acpi_ex_dump_operand (*element);
			}
		}
		acpi_os_printf ("\n");
		break;


	case ACPI_TYPE_REGION:

		acpi_os_printf ("Region %s (%X)",
			acpi_ut_get_region_name (obj_desc->region.space_id),
			obj_desc->region.space_id);

		/*
		 * If the address and length have not been evaluated,
		 * don't print them.
		 */
		if (!(obj_desc->region.flags & AOPOBJ_DATA_VALID)) {
			acpi_os_printf ("\n");
		}
		else {
			acpi_os_printf (" base %8.8X%8.8X Length %X\n",
				ACPI_HIDWORD (obj_desc->region.address),
				ACPI_LODWORD (obj_desc->region.address),
				obj_desc->region.length);
		}
		break;


	case ACPI_TYPE_STRING:

		acpi_os_printf ("String length %X @ %p \"",
				 obj_desc->string.length, obj_desc->string.pointer);

		for (i = 0; i < obj_desc->string.length; i++) {
			acpi_os_printf ("%c",
					 obj_desc->string.pointer[i]);
		}
		acpi_os_printf ("\"\n");
		break;


	case INTERNAL_TYPE_BANK_FIELD:

		acpi_os_printf ("Bank_field\n");
		break;


	case INTERNAL_TYPE_REGION_FIELD:

		acpi_os_printf (
			"Region_field: Bits=%X Acc_width=%X Lock=%X Update=%X at byte=%X bit=%X of below:\n",
			obj_desc->field.bit_length, obj_desc->field.access_byte_width,
			obj_desc->field.field_flags & AML_FIELD_LOCK_RULE_MASK,
			obj_desc->field.field_flags & AML_FIELD_UPDATE_RULE_MASK,
			obj_desc->field.base_byte_offset, obj_desc->field.start_field_bit_offset);
		ACPI_DUMP_STACK_ENTRY (obj_desc->field.region_obj);
		break;


	case INTERNAL_TYPE_INDEX_FIELD:

		acpi_os_printf ("Index_field\n");
		break;


	case ACPI_TYPE_BUFFER_FIELD:

		acpi_os_printf (
			"Buffer_field: %X bits at byte %X bit %X of \n",
			obj_desc->buffer_field.bit_length, obj_desc->buffer_field.base_byte_offset,
			obj_desc->buffer_field.start_field_bit_offset);

		if (!obj_desc->buffer_field.buffer_obj)
		{
			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "*NULL* \n"));
		}
		else if (ACPI_GET_OBJECT_TYPE (obj_desc->buffer_field.buffer_obj) != ACPI_TYPE_BUFFER)
		{
			acpi_os_printf ("*not a Buffer* \n");
		}
		else
		{
			ACPI_DUMP_STACK_ENTRY (obj_desc->buffer_field.buffer_obj);
		}

		break;


	case ACPI_TYPE_EVENT:

		acpi_os_printf ("Event\n");
		break;


	case ACPI_TYPE_METHOD:

		acpi_os_printf (
			"Method(%X) @ %p:%X\n",
			obj_desc->method.param_count,
			obj_desc->method.aml_start, obj_desc->method.aml_length);
		break;


	case ACPI_TYPE_MUTEX:

		acpi_os_printf ("Mutex\n");
		break;


	case ACPI_TYPE_DEVICE:

		acpi_os_printf ("Device\n");
		break;


	case ACPI_TYPE_POWER:

		acpi_os_printf ("Power\n");
		break;


	case ACPI_TYPE_PROCESSOR:

		acpi_os_printf ("Processor\n");
		break;


	case ACPI_TYPE_THERMAL:

		acpi_os_printf ("Thermal\n");
		break;


	default:
		/* Unknown Type */

		acpi_os_printf ("Unknown Type %X\n", ACPI_GET_OBJECT_TYPE (obj_desc));
		break;
	}

	return;
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
	acpi_interpreter_mode   interpreter_mode,
	NATIVE_CHAR             *ident,
	u32                     num_levels,
	NATIVE_CHAR             *note,
	NATIVE_CHAR             *module_name,
	u32                     line_number)
{
	NATIVE_UINT             i;
	acpi_operand_object     **obj_desc;


	ACPI_FUNCTION_NAME ("Ex_dump_operands");


	if (!ident)
	{
		ident = "?";
	}

	if (!note)
	{
		note = "?";
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"************* Operand Stack Contents (Opcode [%s], %d Operands)\n",
		ident, num_levels));

	if (num_levels == 0)
	{
		num_levels = 1;
	}

	/* Dump the operand stack starting at the top */

	for (i = 0; num_levels > 0; i--, num_levels--)
	{
		obj_desc = &operands[i];
		acpi_ex_dump_operand (*obj_desc);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"************* Stack dump from %s(%d), %s\n",
		module_name, line_number, note));
	return;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_out*
 *
 * PARAMETERS:  Title               - Descriptive text
 *              Value               - Value to be displayed
 *
 * DESCRIPTION: Object dump output formatting functions.  These functions
 *              reduce the number of format strings required and keeps them
 *              all in one place for easy modification.
 *
 ****************************************************************************/

void
acpi_ex_out_string (
	char                    *title,
	char                    *value)
{
	acpi_os_printf ("%20s : %s\n", title, value);
}

void
acpi_ex_out_pointer (
	char                    *title,
	void                    *value)
{
	acpi_os_printf ("%20s : %p\n", title, value);
}

void
acpi_ex_out_integer (
	char                    *title,
	u32                     value)
{
	acpi_os_printf ("%20s : %X\n", title, value);
}

void
acpi_ex_out_address (
	char                    *title,
	ACPI_PHYSICAL_ADDRESS   value)
{

#if ACPI_MACHINE_WIDTH == 16
	acpi_os_printf ("%20s : %p\n", title, value);
#else
	acpi_os_printf ("%20s : %8.8X%8.8X\n", title,
			 ACPI_HIDWORD (value), ACPI_LODWORD (value));
#endif
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

	ACPI_FUNCTION_ENTRY ();


	if (!flags)
	{
		if (!((ACPI_LV_OBJECTS & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer)))
		{
			return;
		}
	}

	acpi_os_printf ("%20s : %4.4s\n",     "Name", node->name.ascii);
	acpi_ex_out_string ("Type",           acpi_ut_get_type_name (node->type));
	acpi_ex_out_integer ("Flags",         node->flags);
	acpi_ex_out_integer ("Owner Id",      node->owner_id);
	acpi_ex_out_integer ("Reference Count", node->reference_count);
	acpi_ex_out_pointer ("Attached Object", acpi_ns_get_attached_object (node));
	acpi_ex_out_pointer ("Child_list",    node->child);
	acpi_ex_out_pointer ("Next_peer",     node->peer);
	acpi_ex_out_pointer ("Parent",        acpi_ns_get_parent_node (node));
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
	u32                     i;


	ACPI_FUNCTION_TRACE ("Ex_dump_object_descriptor");


	if (!flags)
	{
		if (!((ACPI_LV_OBJECTS & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer)))
		{
			return_VOID;
		}
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) != ACPI_DESC_TYPE_OPERAND)
	{
		acpi_os_printf ("Ex_dump_object_descriptor: %p is not a valid ACPI object\n", obj_desc);
		return_VOID;
	}

	/* Common Fields */

	acpi_ex_out_string ("Type",          acpi_ut_get_object_type_name (obj_desc));
	acpi_ex_out_integer ("Reference Count", obj_desc->common.reference_count);
	acpi_ex_out_integer ("Flags",        obj_desc->common.flags);

	/* Object-specific Fields */

	switch (ACPI_GET_OBJECT_TYPE (obj_desc))
	{
	case ACPI_TYPE_INTEGER:

		acpi_os_printf ("%20s : %8.8X%8.8X\n", "Value",
				  ACPI_HIDWORD (obj_desc->integer.value),
				  ACPI_LODWORD (obj_desc->integer.value));
		break;


	case ACPI_TYPE_STRING:

		acpi_ex_out_integer ("Length",       obj_desc->string.length);
		acpi_ex_out_pointer ("Pointer",      obj_desc->string.pointer);
		break;


	case ACPI_TYPE_BUFFER:

		acpi_ex_out_integer ("Length",       obj_desc->buffer.length);
		acpi_ex_out_pointer ("Pointer",      obj_desc->buffer.pointer);
		break;


	case ACPI_TYPE_PACKAGE:

		acpi_ex_out_integer ("Flags",        obj_desc->package.flags);
		acpi_ex_out_integer ("Count",        obj_desc->package.count);
		acpi_ex_out_pointer ("Elements",     obj_desc->package.elements);

		/* Dump the package contents */

		if (obj_desc->package.count > 0)
		{
			acpi_os_printf ("\nPackage Contents:\n");
			for (i = 0; i < obj_desc->package.count; i++)
			{
				acpi_os_printf ("[%.3d] %p", i, obj_desc->package.elements[i]);
				if (obj_desc->package.elements[i])
				{
					acpi_os_printf (" %s", acpi_ut_get_object_type_name (obj_desc->package.elements[i]));
				}
				acpi_os_printf ("\n");
			}
		}
		break;


	case ACPI_TYPE_DEVICE:

		acpi_ex_out_pointer ("Addr_handler", obj_desc->device.addr_handler);
		acpi_ex_out_pointer ("Sys_handler",  obj_desc->device.sys_handler);
		acpi_ex_out_pointer ("Drv_handler",  obj_desc->device.drv_handler);
		break;


	case ACPI_TYPE_EVENT:

		acpi_ex_out_pointer ("Semaphore",    obj_desc->event.semaphore);
		break;


	case ACPI_TYPE_METHOD:

		acpi_ex_out_integer ("Param_count",  obj_desc->method.param_count);
		acpi_ex_out_integer ("Concurrency",  obj_desc->method.concurrency);
		acpi_ex_out_pointer ("Semaphore",    obj_desc->method.semaphore);
		acpi_ex_out_integer ("Owning_id",    obj_desc->method.owning_id);
		acpi_ex_out_integer ("Aml_length",   obj_desc->method.aml_length);
		acpi_ex_out_pointer ("Aml_start",    obj_desc->method.aml_start);
		break;


	case ACPI_TYPE_MUTEX:

		acpi_ex_out_integer ("Sync_level",   obj_desc->mutex.sync_level);
		acpi_ex_out_pointer ("Owner_thread", obj_desc->mutex.owner_thread);
		acpi_ex_out_integer ("Acquisition_depth",obj_desc->mutex.acquisition_depth);
		acpi_ex_out_pointer ("Semaphore",    obj_desc->mutex.semaphore);
		break;


	case ACPI_TYPE_REGION:

		acpi_ex_out_integer ("Space_id",     obj_desc->region.space_id);
		acpi_ex_out_integer ("Flags",        obj_desc->region.flags);
		acpi_ex_out_address ("Address",      obj_desc->region.address);
		acpi_ex_out_integer ("Length",       obj_desc->region.length);
		acpi_ex_out_pointer ("Addr_handler", obj_desc->region.addr_handler);
		acpi_ex_out_pointer ("Next",         obj_desc->region.next);
		break;


	case ACPI_TYPE_POWER:

		acpi_ex_out_integer ("System_level", obj_desc->power_resource.system_level);
		acpi_ex_out_integer ("Resource_order", obj_desc->power_resource.resource_order);
		acpi_ex_out_pointer ("Sys_handler",  obj_desc->power_resource.sys_handler);
		acpi_ex_out_pointer ("Drv_handler",  obj_desc->power_resource.drv_handler);
		break;


	case ACPI_TYPE_PROCESSOR:

		acpi_ex_out_integer ("Processor ID", obj_desc->processor.proc_id);
		acpi_ex_out_integer ("Length",       obj_desc->processor.length);
		acpi_ex_out_address ("Address",      (ACPI_PHYSICAL_ADDRESS) obj_desc->processor.address);
		acpi_ex_out_pointer ("Sys_handler",  obj_desc->processor.sys_handler);
		acpi_ex_out_pointer ("Drv_handler",  obj_desc->processor.drv_handler);
		acpi_ex_out_pointer ("Addr_handler", obj_desc->processor.addr_handler);
		break;


	case ACPI_TYPE_THERMAL:

		acpi_ex_out_pointer ("Sys_handler",  obj_desc->thermal_zone.sys_handler);
		acpi_ex_out_pointer ("Drv_handler",  obj_desc->thermal_zone.drv_handler);
		acpi_ex_out_pointer ("Addr_handler", obj_desc->thermal_zone.addr_handler);
		break;


	case ACPI_TYPE_BUFFER_FIELD:
	case INTERNAL_TYPE_REGION_FIELD:
	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:

		acpi_ex_out_integer ("Field_flags",  obj_desc->common_field.field_flags);
		acpi_ex_out_integer ("Access_byte_width", obj_desc->common_field.access_byte_width);
		acpi_ex_out_integer ("Bit_length",   obj_desc->common_field.bit_length);
		acpi_ex_out_integer ("Fld_bit_offset", obj_desc->common_field.start_field_bit_offset);
		acpi_ex_out_integer ("Base_byte_offset", obj_desc->common_field.base_byte_offset);
		acpi_ex_out_integer ("Datum_valid_bits", obj_desc->common_field.datum_valid_bits);
		acpi_ex_out_integer ("End_fld_valid_bits", obj_desc->common_field.end_field_valid_bits);
		acpi_ex_out_integer ("End_buf_valid_bits", obj_desc->common_field.end_buffer_valid_bits);
		acpi_ex_out_pointer ("Parent_node",  obj_desc->common_field.node);

		switch (ACPI_GET_OBJECT_TYPE (obj_desc))
		{
		case ACPI_TYPE_BUFFER_FIELD:
			acpi_ex_out_pointer ("Buffer_obj",   obj_desc->buffer_field.buffer_obj);
			break;

		case INTERNAL_TYPE_REGION_FIELD:
			acpi_ex_out_pointer ("Region_obj",   obj_desc->field.region_obj);
			break;

		case INTERNAL_TYPE_BANK_FIELD:
			acpi_ex_out_integer ("Value",        obj_desc->bank_field.value);
			acpi_ex_out_pointer ("Region_obj",   obj_desc->bank_field.region_obj);
			acpi_ex_out_pointer ("Bank_obj",     obj_desc->bank_field.bank_obj);
			break;

		case INTERNAL_TYPE_INDEX_FIELD:
			acpi_ex_out_integer ("Value",        obj_desc->index_field.value);
			acpi_ex_out_pointer ("Index",        obj_desc->index_field.index_obj);
			acpi_ex_out_pointer ("Data",         obj_desc->index_field.data_obj);
			break;

		default:
			/* All object types covered above */
			break;
		}
		break;


	case INTERNAL_TYPE_REFERENCE:

		acpi_ex_out_integer ("Target_type",  obj_desc->reference.target_type);
		acpi_ex_out_string ("Opcode",        (acpi_ps_get_opcode_info (obj_desc->reference.opcode))->name);
		acpi_ex_out_integer ("Offset",       obj_desc->reference.offset);
		acpi_ex_out_pointer ("Obj_desc",     obj_desc->reference.object);
		acpi_ex_out_pointer ("Node",         obj_desc->reference.node);
		acpi_ex_out_pointer ("Where",        obj_desc->reference.where);
		break;


	case INTERNAL_TYPE_ADDRESS_HANDLER:

		acpi_ex_out_integer ("Space_id",     obj_desc->addr_handler.space_id);
		acpi_ex_out_pointer ("Next",         obj_desc->addr_handler.next);
		acpi_ex_out_pointer ("Region_list",  obj_desc->addr_handler.region_list);
		acpi_ex_out_pointer ("Node",         obj_desc->addr_handler.node);
		acpi_ex_out_pointer ("Context",      obj_desc->addr_handler.context);
		break;


	case INTERNAL_TYPE_NOTIFY:

		acpi_ex_out_pointer ("Node",         obj_desc->notify_handler.node);
		acpi_ex_out_pointer ("Context",      obj_desc->notify_handler.context);
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
	case INTERNAL_TYPE_EXTRA:
	case INTERNAL_TYPE_DATA:
	default:

		acpi_os_printf ("Ex_dump_object_descriptor: Display not implemented for object type %s\n",
			acpi_ut_get_object_type_name (obj_desc));
		break;
	}

	return_VOID;
}

#endif

