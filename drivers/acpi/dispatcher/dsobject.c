/******************************************************************************
 *
 * Module Name: dsobject - Dispatcher object management routines
 *              $Revision: 81 $
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
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dsobject")


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
 *              within the  namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Op Regions
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_one_object (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value)
{
	acpi_object_type8       type;
	acpi_status             status;
	acpi_init_walk_info     *info = (acpi_init_walk_info *) context;
	u8                      table_revision;


	PROC_NAME ("Ds_init_one_object");


	info->object_count++;
	table_revision = info->table_desc->pointer->revision;

	/*
	 * We are only interested in objects owned by the table that
	 * was just loaded
	 */
	if (((acpi_namespace_node *) obj_handle)->owner_id !=
			info->table_desc->table_id) {
		return (AE_OK);
	}


	/* And even then, we are only interested in a few object types */

	type = acpi_ns_get_type (obj_handle);

	switch (type) {

	case ACPI_TYPE_REGION:

		acpi_ds_initialize_region (obj_handle);

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
		 */
		if (table_revision == 1) {
			((acpi_namespace_node *)obj_handle)->flags |= ANOBJ_DATA_WIDTH_32;
		}

		/*
		 * Always parse methods to detect errors, we may delete
		 * the parse tree below
		 */
		status = acpi_ds_parse_method (obj_handle);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Method %p [%4.4s] - parse failure, %s\n",
				obj_handle, (char*)&((acpi_namespace_node *)obj_handle)->name,
				acpi_format_exception (status)));

			/* This parse failed, but we will continue parsing more methods */

			break;
		}

		/*
		 * Delete the parse tree.  We simple re-parse the method
		 * for every execution since there isn't much overhead
		 */
		acpi_ns_delete_namespace_subtree (obj_handle);
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
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the entire namespace and perform any necessary
 *              initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status
acpi_ds_initialize_objects (
	acpi_table_desc         *table_desc,
	acpi_namespace_node     *start_node)
{
	acpi_status             status;
	acpi_init_walk_info     info;


	FUNCTION_TRACE ("Ds_initialize_objects");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"**** Starting initialization of namespace objects ****\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "Parsing Methods:"));


	info.method_count   = 0;
	info.op_region_count = 0;
	info.object_count   = 0;
	info.table_desc     = table_desc;


	/* Walk entire namespace from the supplied root */

	status = acpi_walk_namespace (ACPI_TYPE_ANY, start_node, ACPI_UINT32_MAX,
			  acpi_ds_init_one_object, &info, NULL);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Walk_namespace failed! %x\n", status));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK,
		"\n%d Control Methods found and parsed (%d nodes total)\n",
		info.method_count, info.object_count));
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%d Control Methods found\n", info.method_count));
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%d Op Regions found\n", info.op_region_count));

	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_init_object_from_op
 *
 * PARAMETERS:  Op              - Parser op used to init the internal object
 *              Opcode          - AML opcode associated with the object
 *              Obj_desc        - Namespace object to be initialized
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
	acpi_status             status;
	acpi_parse_object       *arg;
	acpi_parse2_object      *byte_list;
	acpi_operand_object     *arg_desc;
	const acpi_opcode_info  *op_info;
	acpi_operand_object     *obj_desc;


	PROC_NAME ("Ds_init_object_from_op");


	obj_desc = *ret_obj_desc;
	op_info = acpi_ps_get_opcode_info (opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		/* Unknown opcode */

		return (AE_TYPE);
	}


	/* Get and prepare the first argument */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_BUFFER:

		/* First arg is a number */

		acpi_ds_create_operand (walk_state, op->value.arg, 0);
		arg_desc = walk_state->operands [walk_state->num_operands - 1];
		acpi_ds_obj_stack_pop (1, walk_state);

		/* Resolve the object (could be an arg or local) */

		status = acpi_ex_resolve_to_value (&arg_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			acpi_ut_remove_reference (arg_desc);
			return (status);
		}

		/* We are expecting a number */

		if (arg_desc->common.type != ACPI_TYPE_INTEGER) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Expecting number, got obj: %p type %X\n",
				arg_desc, arg_desc->common.type));
			acpi_ut_remove_reference (arg_desc);
			return (AE_TYPE);
		}

		/* Get the value, delete the internal object */

		obj_desc->buffer.length = (u32) arg_desc->integer.value;
		acpi_ut_remove_reference (arg_desc);

		/* Allocate the buffer */

		if (obj_desc->buffer.length == 0) {
			obj_desc->buffer.pointer = NULL;
			REPORT_WARNING (("Buffer created with zero length in AML\n"));
			break;
		}

		else {
			obj_desc->buffer.pointer = ACPI_MEM_CALLOCATE (
					   obj_desc->buffer.length);

			if (!obj_desc->buffer.pointer) {
				return (AE_NO_MEMORY);
			}
		}

		/*
		 * Second arg is the buffer data (optional) Byte_list can be either
		 * individual bytes or a string initializer.
		 */
		arg = op->value.arg;         /* skip first arg */

		byte_list = (acpi_parse2_object *) arg->next;
		if (byte_list) {
			if (byte_list->opcode != AML_INT_BYTELIST_OP) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Expecting bytelist, got: %p\n",
					byte_list));
				return (AE_TYPE);
			}

			MEMCPY (obj_desc->buffer.pointer, byte_list->data,
					obj_desc->buffer.length);
		}

		break;


	case ACPI_TYPE_PACKAGE:

		/*
		 * When called, an internal package object has already been built and
		 * is pointed to by Obj_desc. Acpi_ds_build_internal_object builds another
		 * internal package object, so remove reference to the original so
		 * that it is deleted.  Error checking is done within the remove
		 * reference function.
		 */
		acpi_ut_remove_reference (obj_desc);
		status = acpi_ds_build_internal_object (walk_state, op, ret_obj_desc);
		break;

	case ACPI_TYPE_INTEGER:
		obj_desc->integer.value = op->value.integer;
		break;


	case ACPI_TYPE_STRING:
		obj_desc->string.pointer = op->value.string;
		obj_desc->string.length = STRLEN (op->value.string);

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
			break;


		case AML_TYPE_METHOD_ARGUMENT:

			/* Split the opcode into a base opcode + offset */

			obj_desc->reference.opcode = AML_ARG_OP;
			obj_desc->reference.offset = opcode - AML_ARG_OP;
			break;


		default: /* Constants, Literals, etc.. */

			if (op->opcode == AML_INT_NAMEPATH_OP) {
				/* Node was saved in Op */

				obj_desc->reference.node = op->node;
			}

			obj_desc->reference.opcode = opcode;
			break;
		}

		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unimplemented data type: %x\n",
			obj_desc->common.type));

		break;
	}

	return (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_build_internal_simple_obj
 *
 * PARAMETERS:  Op              - Parser object to be translated
 *              Obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op object to the equivalent namespace object
 *              Simple objects are any objects other than a package object!
 *
 ****************************************************************************/

static acpi_status
acpi_ds_build_internal_simple_obj (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_operand_object     **obj_desc_ptr)
{
	acpi_operand_object     *obj_desc;
	acpi_object_type8       type;
	acpi_status             status;
	u32                     length;
	char                    *name;


	FUNCTION_TRACE ("Ds_build_internal_simple_obj");


	if (op->opcode == AML_INT_NAMEPATH_OP) {
		/*
		 * This is an object reference.  If The name was
		 * previously looked up in the NS, it is stored in this op.
		 * Otherwise, go ahead and look it up now
		 */
		if (!op->node) {
			status = acpi_ns_lookup (walk_state->scope_info,
					  op->value.string, ACPI_TYPE_ANY,
					  IMODE_EXECUTE,
					  NS_SEARCH_PARENT | NS_DONT_OPEN_SCOPE,
					  NULL,
					  (acpi_namespace_node **)&(op->node));

			if (ACPI_FAILURE (status)) {
				if (status == AE_NOT_FOUND) {
					name = NULL;
					acpi_ns_externalize_name (ACPI_UINT32_MAX, op->value.string, &length, &name);

					if (name) {
						REPORT_WARNING (("Reference %s at AML %X not found\n",
								 name, op->aml_offset));
						ACPI_MEM_FREE (name);
					}

					else {
						REPORT_WARNING (("Reference %s at AML %X not found\n",
								   op->value.string, op->aml_offset));
					}

					*obj_desc_ptr = NULL;
				}

				else {
					return_ACPI_STATUS (status);
				}
			}
		}

		/*
		 * The reference will be a Reference
		 * TBD: [Restructure] unless we really need a separate
		 *  type of INTERNAL_TYPE_REFERENCE change
		 *  Acpi_ds_map_opcode_to_data_type to handle this case
		 */
		type = INTERNAL_TYPE_REFERENCE;
	}
	else {
		type = acpi_ds_map_opcode_to_data_type (op->opcode, NULL);
	}


	/* Create and init the internal ACPI object */

	obj_desc = acpi_ut_create_internal_object (type);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_object_from_op (walk_state, op, op->opcode, &obj_desc);
	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (obj_desc);
		return_ACPI_STATUS (status);
	}

	*obj_desc_ptr = obj_desc;

	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_build_internal_package_obj
 *
 * PARAMETERS:  Op              - Parser object to be translated
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
	acpi_operand_object     **obj_desc_ptr)
{
	acpi_parse_object       *arg;
	acpi_operand_object     *obj_desc;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ds_build_internal_package_obj");


	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_PACKAGE);
	*obj_desc_ptr = obj_desc;
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	if (op->opcode == AML_VAR_PACKAGE_OP) {
		/*
		 * Variable length package parameters are evaluated JIT
		 */
		return_ACPI_STATUS (AE_OK);
	}

	/* The first argument must be the package length */

	arg = op->value.arg;
	obj_desc->package.count = arg->value.integer32;

	/*
	 * Allocate the array of pointers (ptrs to the
	 * individual objects) Add an extra pointer slot so
	 * that the list is always null terminated.
	 */
	obj_desc->package.elements = ACPI_MEM_CALLOCATE (
			 (obj_desc->package.count + 1) * sizeof (void *));

	if (!obj_desc->package.elements) {
		acpi_ut_delete_object_desc (obj_desc);
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	obj_desc->package.next_element = obj_desc->package.elements;

	/*
	 * Now init the elements of the package
	 */
	arg = arg->next;
	while (arg) {
		if (arg->opcode == AML_PACKAGE_OP) {
			status = acpi_ds_build_internal_package_obj (walk_state, arg,
					  obj_desc->package.next_element);
		}

		else {
			status = acpi_ds_build_internal_simple_obj (walk_state, arg,
					  obj_desc->package.next_element);
		}

		obj_desc->package.next_element++;
		arg = arg->next;
	}

	obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_build_internal_object
 *
 * PARAMETERS:  Op              - Parser object to be translated
 *              Obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op object to the equivalent namespace
 *              object
 *
 ****************************************************************************/

acpi_status
acpi_ds_build_internal_object (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_operand_object     **obj_desc_ptr)
{
	acpi_status             status;


	switch (op->opcode) {
	case AML_PACKAGE_OP:
	case AML_VAR_PACKAGE_OP:

		status = acpi_ds_build_internal_package_obj (walk_state, op, obj_desc_ptr);
		break;


	default:

		status = acpi_ds_build_internal_simple_obj (walk_state, op, obj_desc_ptr);
		break;
	}

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_node
 *
 * PARAMETERS:  Op              - Parser object to be translated
 *              Obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
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


	FUNCTION_TRACE_PTR ("Ds_create_node", op);


	/*
	 * Because of the execution pass through the non-control-method
	 * parts of the table, we can arrive here twice.  Only init
	 * the named object node the first time through
	 */
	if (node->object) {
		return_ACPI_STATUS (AE_OK);
	}

	if (!op->value.arg) {
		/* No arguments, there is nothing to do */

		return_ACPI_STATUS (AE_OK);
	}

	/* Build an internal object for the argument(s) */

	status = acpi_ds_build_internal_object (walk_state, op->value.arg, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Re-type the object according to it's argument */

	node->type = obj_desc->common.type;

	/* Init obj */

	status = acpi_ns_attach_object (node, obj_desc, (u8) node->type);

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


