/******************************************************************************
 *
 * Module Name: dsobject - Dispatcher object management routines
 *              $Revision: 106 $
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
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dsobject")


#ifndef ACPI_NO_METHOD_EXECUTION
/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_init_one_object
 *
 * PARAMETERS:  Obj_handle      - Node
 *              Level           - Current nesting level
 *              Context         - Points to a init info struct
 *              Return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from Acpi_walk_namespace. Invoked for every object
 *              within the namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Operation Regions
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_one_object (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value)
{
	acpi_object_type        type;
	acpi_status             status;
	acpi_init_walk_info     *info = (acpi_init_walk_info *) context;


	ACPI_FUNCTION_NAME ("Ds_init_one_object");


	/*
	 * We are only interested in objects owned by the table that
	 * was just loaded
	 */
	if (((acpi_namespace_node *) obj_handle)->owner_id !=
			info->table_desc->table_id) {
		return (AE_OK);
	}

	info->object_count++;

	/* And even then, we are only interested in a few object types */

	type = acpi_ns_get_type (obj_handle);

	switch (type) {
	case ACPI_TYPE_REGION:

		status = acpi_ds_initialize_region (obj_handle);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Region %p [%4.4s] - Init failure, %s\n",
				obj_handle, ((acpi_namespace_node *) obj_handle)->name.ascii,
				acpi_format_exception (status)));
		}

		info->op_region_count++;
		break;


	case ACPI_TYPE_METHOD:

		info->method_count++;

		if (!(acpi_dbg_level & ACPI_LV_INIT)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "."));
		}

		/*
		 * Set the execution data width (32 or 64) based upon the
		 * revision number of the parent ACPI table.
		 * TBD: This is really for possible future support of integer width
		 * on a per-table basis. Currently, we just use a global for the width.
		 */
		if (info->table_desc->pointer->revision == 1) {
			((acpi_namespace_node *) obj_handle)->flags |= ANOBJ_DATA_WIDTH_32;
		}

		/*
		 * Always parse methods to detect errors, we may delete
		 * the parse tree below
		 */
		status = acpi_ds_parse_method (obj_handle);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Method %p [%4.4s] - parse failure, %s\n",
				obj_handle, ((acpi_namespace_node *) obj_handle)->name.ascii,
				acpi_format_exception (status)));

			/* This parse failed, but we will continue parsing more methods */

			break;
		}

		/*
		 * Delete the parse tree.  We simple re-parse the method
		 * for every execution since there isn't much overhead
		 */
		acpi_ns_delete_namespace_subtree (obj_handle);
		acpi_ns_delete_namespace_by_owner (((acpi_namespace_node *) obj_handle)->object->method.owning_id);
		break;


	case ACPI_TYPE_DEVICE:

		info->device_count++;
		break;


	default:
		break;
	}

	/*
	 * We ignore errors from above, and always return OK, since
	 * we don't want to abort the walk on a single error.
	 */
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_initialize_objects
 *
 * PARAMETERS:  Table_desc      - Descriptor for parent ACPI table
 *              Start_node      - Root of subtree to be initialized.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the namespace starting at "Start_node" and perform any
 *              necessary initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status
acpi_ds_initialize_objects (
	acpi_table_desc         *table_desc,
	acpi_namespace_node     *start_node)
{
	acpi_status             status;
	acpi_init_walk_info     info;


	ACPI_FUNCTION_TRACE ("Ds_initialize_objects");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"**** Starting initialization of namespace objects ****\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "Parsing Methods:"));

	info.method_count   = 0;
	info.op_region_count = 0;
	info.object_count   = 0;
	info.device_count   = 0;
	info.table_desc     = table_desc;

	/* Walk entire namespace from the supplied root */

	status = acpi_walk_namespace (ACPI_TYPE_ANY, start_node, ACPI_UINT32_MAX,
			  acpi_ds_init_one_object, &info, NULL);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Walk_namespace failed, %s\n",
			acpi_format_exception (status)));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK,
		"\nTable [%4.4s] - %hd Objects with %hd Devices %hd Methods %hd Regions\n",
		table_desc->pointer->signature, info.object_count,
		info.device_count, info.method_count, info.op_region_count));

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%hd Methods, %hd Regions\n", info.method_count, info.op_region_count));

	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_build_internal_object
 *
 * PARAMETERS:  Walk_state      - Current walk state
 *              Op              - Parser object to be translated
 *              Obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op object to the equivalent namespace object
 *              Simple objects are any objects other than a package object!
 *
 ****************************************************************************/

acpi_status
acpi_ds_build_internal_object (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_operand_object     **obj_desc_ptr)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;
	char                    *name;


	ACPI_FUNCTION_TRACE ("Ds_build_internal_object");


	if (op->common.aml_opcode == AML_INT_NAMEPATH_OP) {
		/*
		 * This is an named object reference.  If this name was
		 * previously looked up in the namespace, it was stored in this op.
		 * Otherwise, go ahead and look it up now
		 */
		if (!op->common.node) {
			status = acpi_ns_lookup (walk_state->scope_info, op->common.value.string,
					  ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
					  ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE, NULL,
					  (acpi_namespace_node **) &(op->common.node));

			if (ACPI_FAILURE (status)) {
				if (status == AE_NOT_FOUND) {
					name = NULL;
					status = acpi_ns_externalize_name (ACPI_UINT32_MAX, op->common.value.string, NULL, &name);
					if (ACPI_SUCCESS (status)) {
						ACPI_REPORT_WARNING (("Reference %s at AML %X not found\n",
								 name, op->common.aml_offset));
						ACPI_MEM_FREE (name);
					}
					else {
						ACPI_REPORT_WARNING (("Reference %s at AML %X not found\n",
								   op->common.value.string, op->common.aml_offset));
					}

					*obj_desc_ptr = NULL;
				}
				else {
					return_ACPI_STATUS (status);
				}
			}
		}
	}

	/* Create and init the internal ACPI object */

	obj_desc = acpi_ut_create_internal_object ((acpi_ps_get_opcode_info (op->common.aml_opcode))->object_type);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_object_from_op (walk_state, op, op->common.aml_opcode, &obj_desc);
	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (obj_desc);
		return_ACPI_STATUS (status);
	}

	*obj_desc_ptr = obj_desc;
	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_build_internal_buffer_obj
 *
 * PARAMETERS:  Walk_state      - Current walk state
 *              Op              - Parser object to be translated
 *              Buffer_length   - Length of the buffer
 *              Obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op package object to the equivalent
 *              namespace object
 *
 ****************************************************************************/

acpi_status
acpi_ds_build_internal_buffer_obj (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	u32                     buffer_length,
	acpi_operand_object     **obj_desc_ptr)
{
	acpi_parse_object       *arg;
	acpi_operand_object     *obj_desc;
	acpi_parse_object       *byte_list;
	u32                     byte_list_length = 0;


	ACPI_FUNCTION_TRACE ("Ds_build_internal_buffer_obj");


	obj_desc = *obj_desc_ptr;
	if (obj_desc) {
		/*
		 * We are evaluating a Named buffer object "Name (xxxx, Buffer)".
		 * The buffer object already exists (from the NS node)
		 */
	}
	else {
		/* Create a new buffer object */

		obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		*obj_desc_ptr = obj_desc;
		if (!obj_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}
	}

	/*
	 * Second arg is the buffer data (optional) Byte_list can be either
	 * individual bytes or a string initializer.  In either case, a
	 * Byte_list appears in the AML.
	 */
	arg = op->common.value.arg;         /* skip first arg */

	byte_list = arg->named.next;
	if (byte_list) {
		if (byte_list->common.aml_opcode != AML_INT_BYTELIST_OP) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Expecting bytelist, got AML opcode %X in op %p\n",
				byte_list->common.aml_opcode, byte_list));

			acpi_ut_remove_reference (obj_desc);
			return (AE_TYPE);
		}

		byte_list_length = byte_list->common.value.integer32;
	}

	/*
	 * The buffer length (number of bytes) will be the larger of:
	 * 1) The specified buffer length and
	 * 2) The length of the initializer byte list
	 */
	obj_desc->buffer.length = buffer_length;
	if (byte_list_length > buffer_length) {
		obj_desc->buffer.length = byte_list_length;
	}

	/* Allocate the buffer */

	if (obj_desc->buffer.length == 0) {
		obj_desc->buffer.pointer = NULL;
		ACPI_REPORT_WARNING (("Buffer created with zero length in AML\n"));
		return_ACPI_STATUS (AE_OK);
	}

	obj_desc->buffer.pointer = ACPI_MEM_CALLOCATE (
			   obj_desc->buffer.length);
	if (!obj_desc->buffer.pointer) {
		acpi_ut_delete_object_desc (obj_desc);
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Initialize buffer from the Byte_list (if present) */

	if (byte_list) {
		ACPI_MEMCPY (obj_desc->buffer.pointer, byte_list->named.data,
				  byte_list_length);
	}

	obj_desc->buffer.flags |= AOPOBJ_DATA_VALID;
	op->common.node = (acpi_namespace_node *) obj_desc;
	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_build_internal_package_obj
 *
 * PARAMETERS:  Walk_state      - Current walk state
 *              Op              - Parser object to be translated
 *              Package_length  - Number of elements in the package
 *              Obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op package object to the equivalent
 *              namespace object
 *
 ****************************************************************************/

acpi_status
acpi_ds_build_internal_package_obj (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	u32                     package_length,
	acpi_operand_object     **obj_desc_ptr)
{
	acpi_parse_object       *arg;
	acpi_parse_object       *parent;
	acpi_operand_object     *obj_desc = NULL;
	u32                     package_list_length;
	acpi_status             status = AE_OK;
	u32                     i;


	ACPI_FUNCTION_TRACE ("Ds_build_internal_package_obj");


	/* Find the parent of a possibly nested package */

	parent = op->common.parent;
	while ((parent->common.aml_opcode == AML_PACKAGE_OP)    ||
		   (parent->common.aml_opcode == AML_VAR_PACKAGE_OP)) {
		parent = parent->common.parent;
	}

	obj_desc = *obj_desc_ptr;
	if (obj_desc) {
		/*
		 * We are evaluating a Named package object "Name (xxxx, Package)".
		 * Get the existing package object from the NS node
		 */
	}
	else {
		obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_PACKAGE);
		*obj_desc_ptr = obj_desc;
		if (!obj_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		obj_desc->package.node = parent->common.node;
	}

	obj_desc->package.count = package_length;

	/* Count the number of items in the package list */

	package_list_length = 0;
	arg = op->common.value.arg;
	arg = arg->common.next;
	while (arg) {
		package_list_length++;
		arg = arg->common.next;
	}

	/*
	 * The package length (number of elements) will be the greater
	 * of the specified length and the length of the initializer list
	 */
	if (package_list_length > package_length) {
		obj_desc->package.count = package_list_length;
	}

	/*
	 * Allocate the pointer array (array of pointers to the
	 * individual objects). Add an extra pointer slot so
	 * that the list is always null terminated.
	 */
	obj_desc->package.elements = ACPI_MEM_CALLOCATE (
			 ((ACPI_SIZE) obj_desc->package.count + 1) * sizeof (void *));

	if (!obj_desc->package.elements) {
		acpi_ut_delete_object_desc (obj_desc);
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/*
	 * Now init the elements of the package
	 */
	i = 0;
	arg = op->common.value.arg;
	arg = arg->common.next;
	while (arg) {
		if (arg->common.aml_opcode == AML_INT_RETURN_VALUE_OP) {
			/* Object (package or buffer) is already built */

			obj_desc->package.elements[i] = ACPI_CAST_PTR (acpi_operand_object, arg->common.node);
		}
		else {
			status = acpi_ds_build_internal_object (walk_state, arg,
					  &obj_desc->package.elements[i]);
		}

		i++;
		arg = arg->common.next;
	}

	obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	op->common.node = (acpi_namespace_node *) obj_desc;
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_node
 *
 * PARAMETERS:  Walk_state      - Current walk state
 *              Node            - NS Node to be initialized
 *              Op              - Parser object to be translated
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the object to be associated with a namespace node
 *
 ****************************************************************************/

acpi_status
acpi_ds_create_node (
	acpi_walk_state         *walk_state,
	acpi_namespace_node     *node,
	acpi_parse_object       *op)
{
	acpi_status             status;
	acpi_operand_object     *obj_desc;


	ACPI_FUNCTION_TRACE_PTR ("Ds_create_node", op);


	/*
	 * Because of the execution pass through the non-control-method
	 * parts of the table, we can arrive here twice.  Only init
	 * the named object node the first time through
	 */
	if (acpi_ns_get_attached_object (node)) {
		return_ACPI_STATUS (AE_OK);
	}

	if (!op->common.value.arg) {
		/* No arguments, there is nothing to do */

		return_ACPI_STATUS (AE_OK);
	}

	/* Build an internal object for the argument(s) */

	status = acpi_ds_build_internal_object (walk_state, op->common.value.arg, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Re-type the object according to it's argument */

	node->type = ACPI_GET_OBJECT_TYPE (obj_desc);

	/* Attach obj to node */

	status = acpi_ns_attach_object (node, obj_desc, node->type);

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}

#endif /* ACPI_NO_METHOD_EXECUTION */


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_init_object_from_op
 *
 * PARAMETERS:  Walk_state      - Current walk state
 *              Op              - Parser op used to init the internal object
 *              Opcode          - AML opcode associated with the object
 *              Ret_obj_desc    - Namespace object to be initialized
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize a namespace object from a parser Op and its
 *              associated arguments.  The namespace object is a more compact
 *              representation of the Op and its arguments.
 *
 ****************************************************************************/

acpi_status
acpi_ds_init_object_from_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	u16                     opcode,
	acpi_operand_object     **ret_obj_desc)
{
	const acpi_opcode_info  *op_info;
	acpi_operand_object     *obj_desc;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ds_init_object_from_op");


	obj_desc = *ret_obj_desc;
	op_info = acpi_ps_get_opcode_info (opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		/* Unknown opcode */

		return_ACPI_STATUS (AE_TYPE);
	}

	/* Perform per-object initialization */

	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_BUFFER:

		/*
		 * Defer evaluation of Buffer Term_arg operand
		 */
		obj_desc->buffer.node     = (acpi_namespace_node *) walk_state->operands[0];
		obj_desc->buffer.aml_start = op->named.data;
		obj_desc->buffer.aml_length = op->named.length;
		break;


	case ACPI_TYPE_PACKAGE:

		/*
		 * Defer evaluation of Package Term_arg operand
		 */
		obj_desc->package.node     = (acpi_namespace_node *) walk_state->operands[0];
		obj_desc->package.aml_start = op->named.data;
		obj_desc->package.aml_length = op->named.length;
		break;


	case ACPI_TYPE_INTEGER:

		switch (op_info->type) {
		case AML_TYPE_CONSTANT:
			/*
			 * Resolve AML Constants here - AND ONLY HERE!
			 * All constants are integers.
			 * We mark the integer with a flag that indicates that it started life
			 * as a constant -- so that stores to constants will perform as expected (noop).
			 * (Zero_op is used as a placeholder for optional target operands.)
			 */
			obj_desc->common.flags = AOPOBJ_AML_CONSTANT;

			switch (opcode) {
			case AML_ZERO_OP:

				obj_desc->integer.value = 0;
				break;

			case AML_ONE_OP:

				obj_desc->integer.value = 1;
				break;

			case AML_ONES_OP:

				obj_desc->integer.value = ACPI_INTEGER_MAX;

				/* Truncate value if we are executing from a 32-bit ACPI table */

#ifndef ACPI_NO_METHOD_EXECUTION
				acpi_ex_truncate_for32bit_table (obj_desc);
#endif
				break;

			case AML_REVISION_OP:

				obj_desc->integer.value = ACPI_CA_SUPPORT_LEVEL;
				break;

			default:

				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown constant opcode %X\n", opcode));
				status = AE_AML_OPERAND_TYPE;
				break;
			}
			break;


		case AML_TYPE_LITERAL:

			obj_desc->integer.value = op->common.value.integer;
			break;


		default:
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown Integer type %X\n", op_info->type));
			status = AE_AML_OPERAND_TYPE;
			break;
		}
		break;


	case ACPI_TYPE_STRING:

		obj_desc->string.pointer = op->common.value.string;
		obj_desc->string.length = ACPI_STRLEN (op->common.value.string);

		/*
		 * The string is contained in the ACPI table, don't ever try
		 * to delete it
		 */
		obj_desc->common.flags |= AOPOBJ_STATIC_POINTER;
		break;


	case ACPI_TYPE_METHOD:
		break;


	case INTERNAL_TYPE_REFERENCE:

		switch (op_info->type) {
		case AML_TYPE_LOCAL_VARIABLE:

			/* Split the opcode into a base opcode + offset */

			obj_desc->reference.opcode = AML_LOCAL_OP;
			obj_desc->reference.offset = opcode - AML_LOCAL_OP;
#ifndef ACPI_NO_METHOD_EXECUTION
			acpi_ds_method_data_get_node (AML_LOCAL_OP, obj_desc->reference.offset,
				walk_state, (acpi_namespace_node **) &obj_desc->reference.object);
#endif
			break;


		case AML_TYPE_METHOD_ARGUMENT:

			/* Split the opcode into a base opcode + offset */

			obj_desc->reference.opcode = AML_ARG_OP;
			obj_desc->reference.offset = opcode - AML_ARG_OP;
			break;

		default: /* Other literals, etc.. */

			if (op->common.aml_opcode == AML_INT_NAMEPATH_OP) {
				/* Node was saved in Op */

				obj_desc->reference.node = op->common.node;
			}

			obj_desc->reference.opcode = opcode;
			break;
		}
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unimplemented data type: %X\n",
			ACPI_GET_OBJECT_TYPE (obj_desc)));

		status = AE_AML_OPERAND_TYPE;
		break;
	}

	return_ACPI_STATUS (status);
}


