/******************************************************************************
 *
 * Module Name: dsopcode - Dispatcher Op Region support and handling of
 *                         "control" opcodes
 *              $Revision: 81 $
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
#include "acinterp.h"
#include "acnamesp.h"
#include "acevents.h"

#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dsopcode")


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_execute_arguments
 *
 * PARAMETERS:  Node                - Parent NS node
 *              Aml_length          - Length of executable AML
 *              Aml_start           - Pointer to the AML
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Late execution of region or field arguments
 *
 ****************************************************************************/

acpi_status
acpi_ds_execute_arguments (
	acpi_namespace_node     *node,
	acpi_namespace_node     *scope_node,
	u32                     aml_length,
	u8                      *aml_start)
{
	acpi_status             status;
	acpi_parse_object       *op;
	acpi_walk_state         *walk_state;
	acpi_parse_object       *arg;


	ACPI_FUNCTION_TRACE ("Acpi_ds_execute_arguments");


	/*
	 * Allocate a new parser op to be the root of the parsed tree
	 */
	op = acpi_ps_alloc_op (AML_INT_EVAL_SUBTREE_OP);
	if (!op) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Save the Node for use in Acpi_ps_parse_aml */

	op->common.node = scope_node;

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT, NULL, NULL, NULL);
	if (!walk_state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk (walk_state, op, NULL, aml_start,
			  aml_length, NULL, NULL, 1);
	if (ACPI_FAILURE (status)) {
		acpi_ds_delete_walk_state (walk_state);
		return_ACPI_STATUS (status);
	}

	walk_state->parse_flags = 0;

	/* Pass1: Parse the entire declaration */

	status = acpi_ps_parse_aml (walk_state);
	if (ACPI_FAILURE (status)) {
		acpi_ps_delete_parse_tree (op);
		return_ACPI_STATUS (status);
	}

	/* Get and init the Op created above */

	arg = op->common.value.arg;
	op->common.node = node;
	arg->common.node = node;
	acpi_ps_delete_parse_tree (op);

	/* Evaluate the address and length arguments for the Buffer Field */

	op = acpi_ps_alloc_op (AML_INT_EVAL_SUBTREE_OP);
	if (!op) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	op->common.node = scope_node;

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT, NULL, NULL, NULL);
	if (!walk_state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk (walk_state, op, NULL, aml_start,
			  aml_length, NULL, NULL, 3);
	if (ACPI_FAILURE (status)) {
		acpi_ds_delete_walk_state (walk_state);
		return_ACPI_STATUS (status);
	}

	status = acpi_ps_parse_aml (walk_state);
	acpi_ps_delete_parse_tree (op);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_buffer_field_arguments
 *
 * PARAMETERS:  Obj_desc        - A valid Buffer_field object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get Buffer_field Buffer and Index. This implements the late
 *              evaluation of these field attributes.
 *
 ****************************************************************************/

acpi_status
acpi_ds_get_buffer_field_arguments (
	acpi_operand_object     *obj_desc)
{
	acpi_operand_object     *extra_desc;
	acpi_namespace_node     *node;
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ds_get_buffer_field_arguments", obj_desc);


	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Get the AML pointer (method object) and Buffer_field node */

	extra_desc = acpi_ns_get_secondary_object (obj_desc);
	node = obj_desc->buffer_field.node;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname (node, " [Field]"));
	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[%4.4s] Buffer_field JIT Init\n",
		node->name.ascii));

	/* Execute the AML code for the Term_arg arguments */

	status = acpi_ds_execute_arguments (node, acpi_ns_get_parent_node (node),
			 extra_desc->extra.aml_length, extra_desc->extra.aml_start);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_buffer_arguments
 *
 * PARAMETERS:  Obj_desc        - A valid Bufferobject
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get Buffer length and initializer byte list.  This implements
 *              the late evaluation of these attributes.
 *
 ****************************************************************************/

acpi_status
acpi_ds_get_buffer_arguments (
	acpi_operand_object     *obj_desc)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ds_get_buffer_arguments", obj_desc);


	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Get the Buffer node */

	node = obj_desc->buffer.node;
	if (!node) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"No pointer back to NS node in buffer %p\n", obj_desc));
		return_ACPI_STATUS (AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Buffer JIT Init\n"));

	/* Execute the AML code for the Term_arg arguments */

	status = acpi_ds_execute_arguments (node, node,
			 obj_desc->buffer.aml_length, obj_desc->buffer.aml_start);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_package_arguments
 *
 * PARAMETERS:  Obj_desc        - A valid Packageobject
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get Package length and initializer byte list.  This implements
 *              the late evaluation of these attributes.
 *
 ****************************************************************************/

acpi_status
acpi_ds_get_package_arguments (
	acpi_operand_object     *obj_desc)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ds_get_package_arguments", obj_desc);


	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Get the Package node */

	node = obj_desc->package.node;
	if (!node) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"No pointer back to NS node in package %p\n", obj_desc));
		return_ACPI_STATUS (AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Package JIT Init\n"));

	/* Execute the AML code for the Term_arg arguments */

	status = acpi_ds_execute_arguments (node, node,
			 obj_desc->package.aml_length, obj_desc->package.aml_start);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_region_arguments
 *
 * PARAMETERS:  Obj_desc        - A valid region object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get region address and length.  This implements the late
 *              evaluation of these region attributes.
 *
 ****************************************************************************/

acpi_status
acpi_ds_get_region_arguments (
	acpi_operand_object     *obj_desc)
{
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_operand_object     *extra_desc;


	ACPI_FUNCTION_TRACE_PTR ("Ds_get_region_arguments", obj_desc);


	if (obj_desc->region.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS (AE_OK);
	}

	extra_desc = acpi_ns_get_secondary_object (obj_desc);
	if (!extra_desc) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/* Get the Region node */

	node = obj_desc->region.node;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname (node, " [Operation Region]"));

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[%4.4s] Op_region Init at AML %p\n",
		node->name.ascii, extra_desc->extra.aml_start));


	status = acpi_ds_execute_arguments (node, acpi_ns_get_parent_node (node),
			 extra_desc->extra.aml_length, extra_desc->extra.aml_start);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_initialize_region
 *
 * PARAMETERS:  Op              - A valid region Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Front end to Ev_initialize_region
 *
 ****************************************************************************/

acpi_status
acpi_ds_initialize_region (
	acpi_handle             obj_handle)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	obj_desc = acpi_ns_get_attached_object (obj_handle);

	/* Namespace is NOT locked */

	status = acpi_ev_initialize_region (obj_desc, FALSE);
	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_init_buffer_field
 *
 * PARAMETERS:  Aml_opcode      - Create_xxx_field
 *              Obj_desc        - Buffer_field object
 *              Buffer_desc     - Host Buffer
 *              Offset_desc     - Offset into buffer
 *              Length          - Length of field (CREATE_FIELD_OP only)
 *              Result          - Where to store the result
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform actual initialization of a buffer field
 *
 ****************************************************************************/

acpi_status
acpi_ds_init_buffer_field (
	u16                     aml_opcode,
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *buffer_desc,
	acpi_operand_object     *offset_desc,
	acpi_operand_object     *length_desc,
	acpi_operand_object     *result_desc)
{
	u32                     offset;
	u32                     bit_offset;
	u32                     bit_count;
	u8                      field_flags;
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ds_init_buffer_field", obj_desc);


	/* Host object must be a Buffer */

	if (ACPI_GET_OBJECT_TYPE (buffer_desc) != ACPI_TYPE_BUFFER) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Target of Create Field is not a Buffer object - %s\n",
			acpi_ut_get_object_type_name (buffer_desc)));

		status = AE_AML_OPERAND_TYPE;
		goto cleanup;
	}

	/*
	 * The last parameter to all of these opcodes (Result_desc) started
	 * out as a Name_string, and should therefore now be a NS node
	 * after resolution in Acpi_ex_resolve_operands().
	 */
	if (ACPI_GET_DESCRIPTOR_TYPE (result_desc) != ACPI_DESC_TYPE_NAMED) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "(%s) destination must be a NS Node\n",
			acpi_ps_get_opcode_name (aml_opcode)));

		status = AE_AML_OPERAND_TYPE;
		goto cleanup;
	}

	offset = (u32) offset_desc->integer.value;

	/*
	 * Setup the Bit offsets and counts, according to the opcode
	 */
	switch (aml_opcode) {
	case AML_CREATE_FIELD_OP:

		/* Offset is in bits, count is in bits */

		bit_offset = offset;
		bit_count  = (u32) length_desc->integer.value;
		field_flags = AML_FIELD_ACCESS_BYTE;
		break;

	case AML_CREATE_BIT_FIELD_OP:

		/* Offset is in bits, Field is one bit */

		bit_offset = offset;
		bit_count  = 1;
		field_flags = AML_FIELD_ACCESS_BYTE;
		break;

	case AML_CREATE_BYTE_FIELD_OP:

		/* Offset is in bytes, field is one byte */

		bit_offset = 8 * offset;
		bit_count  = 8;
		field_flags = AML_FIELD_ACCESS_BYTE;
		break;

	case AML_CREATE_WORD_FIELD_OP:

		/* Offset is in bytes, field is one word */

		bit_offset = 8 * offset;
		bit_count  = 16;
		field_flags = AML_FIELD_ACCESS_WORD;
		break;

	case AML_CREATE_DWORD_FIELD_OP:

		/* Offset is in bytes, field is one dword */

		bit_offset = 8 * offset;
		bit_count  = 32;
		field_flags = AML_FIELD_ACCESS_DWORD;
		break;

	case AML_CREATE_QWORD_FIELD_OP:

		/* Offset is in bytes, field is one qword */

		bit_offset = 8 * offset;
		bit_count  = 64;
		field_flags = AML_FIELD_ACCESS_QWORD;
		break;

	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Unknown field creation opcode %02x\n",
			aml_opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}


	/* Entire field must fit within the current length of the buffer */

	if ((bit_offset + bit_count) >
		(8 * (u32) buffer_desc->buffer.length)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Field size %d exceeds Buffer size %d (bits)\n",
			 bit_offset + bit_count, 8 * (u32) buffer_desc->buffer.length));
		status = AE_AML_BUFFER_LIMIT;
		goto cleanup;
	}

	/*
	 * Initialize areas of the field object that are common to all fields
	 * For Field_flags, use LOCK_RULE = 0 (NO_LOCK), UPDATE_RULE = 0 (UPDATE_PRESERVE)
	 */
	status = acpi_ex_prep_common_field_object (obj_desc, field_flags, 0,
			  bit_offset, bit_count);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	obj_desc->buffer_field.buffer_obj = buffer_desc;

	/* Reference count for Buffer_desc inherits Obj_desc count */

	buffer_desc->common.reference_count = (u16) (buffer_desc->common.reference_count +
			  obj_desc->common.reference_count);


cleanup:

	/* Always delete the operands */

	acpi_ut_remove_reference (offset_desc);
	acpi_ut_remove_reference (buffer_desc);

	if (aml_opcode == AML_CREATE_FIELD_OP) {
		acpi_ut_remove_reference (length_desc);
	}

	/* On failure, delete the result descriptor */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (result_desc); /* Result descriptor */
	}
	else {
		/* Now the address and length are valid for this Buffer_field */

		obj_desc->buffer_field.flags |= AOPOBJ_DATA_VALID;
	}

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_eval_buffer_field_operands
 *
 * PARAMETERS:  Walk_state      - Current walk
 *              Op              - A valid Buffer_field Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get Buffer_field Buffer and Index
 *              Called from Acpi_ds_exec_end_op during Buffer_field parse tree walk
 *
 ****************************************************************************/

acpi_status
acpi_ds_eval_buffer_field_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_status             status;
	acpi_operand_object     *obj_desc;
	acpi_namespace_node     *node;
	acpi_parse_object       *next_op;


	ACPI_FUNCTION_TRACE_PTR ("Ds_eval_buffer_field_operands", op);


	/*
	 * This is where we evaluate the address and length fields of the
	 * Create_xxx_field declaration
	 */
	node =  op->common.node;

	/* Next_op points to the op that holds the Buffer */

	next_op = op->common.value.arg;

	/* Evaluate/create the address and length operands */

	status = acpi_ds_create_operands (walk_state, next_op);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	obj_desc = acpi_ns_get_attached_object (node);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/* Resolve the operands */

	status = acpi_ex_resolve_operands (op->common.aml_opcode,
			  ACPI_WALK_OPERANDS, walk_state);

	ACPI_DUMP_OPERANDS (ACPI_WALK_OPERANDS, ACPI_IMODE_EXECUTE,
			  acpi_ps_get_opcode_name (op->common.aml_opcode),
			  walk_state->num_operands, "after Acpi_ex_resolve_operands");

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "(%s) bad operand(s) (%X)\n",
			acpi_ps_get_opcode_name (op->common.aml_opcode), status));

		return_ACPI_STATUS (status);
	}

	/* Initialize the Buffer Field */

	if (op->common.aml_opcode == AML_CREATE_FIELD_OP) {
		/* NOTE: Slightly different operands for this opcode */

		status = acpi_ds_init_buffer_field (op->common.aml_opcode, obj_desc,
				 walk_state->operands[0], walk_state->operands[1],
				 walk_state->operands[2], walk_state->operands[3]);
	}
	else {
		/* All other, Create_xxx_field opcodes */

		status = acpi_ds_init_buffer_field (op->common.aml_opcode, obj_desc,
				 walk_state->operands[0], walk_state->operands[1],
						  NULL, walk_state->operands[2]);
	}

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_eval_region_operands
 *
 * PARAMETERS:  Walk_state      - Current walk
 *              Op              - A valid region Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get region address and length
 *              Called from Acpi_ds_exec_end_op during Op_region parse tree walk
 *
 ****************************************************************************/

acpi_status
acpi_ds_eval_region_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_status             status;
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *operand_desc;
	acpi_namespace_node     *node;
	acpi_parse_object       *next_op;


	ACPI_FUNCTION_TRACE_PTR ("Ds_eval_region_operands", op);


	/*
	 * This is where we evaluate the address and length fields of the Op_region declaration
	 */
	node =  op->common.node;

	/* Next_op points to the op that holds the Space_iD */

	next_op = op->common.value.arg;

	/* Next_op points to address op */

	next_op = next_op->common.next;

	/* Evaluate/create the address and length operands */

	status = acpi_ds_create_operands (walk_state, next_op);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Resolve the length and address operands to numbers */

	status = acpi_ex_resolve_operands (op->common.aml_opcode, ACPI_WALK_OPERANDS, walk_state);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	ACPI_DUMP_OPERANDS (ACPI_WALK_OPERANDS, ACPI_IMODE_EXECUTE,
			  acpi_ps_get_opcode_name (op->common.aml_opcode),
			  1, "after Acpi_ex_resolve_operands");

	obj_desc = acpi_ns_get_attached_object (node);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/*
	 * Get the length operand and save it
	 * (at Top of stack)
	 */
	operand_desc = walk_state->operands[walk_state->num_operands - 1];

	obj_desc->region.length = (u32) operand_desc->integer.value;
	acpi_ut_remove_reference (operand_desc);

	/*
	 * Get the address and save it
	 * (at top of stack - 1)
	 */
	operand_desc = walk_state->operands[walk_state->num_operands - 2];

	obj_desc->region.address = (ACPI_PHYSICAL_ADDRESS) operand_desc->integer.value;
	acpi_ut_remove_reference (operand_desc);

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Rgn_obj %p Addr %8.8X%8.8X Len %X\n",
		obj_desc,
		ACPI_HIDWORD (obj_desc->region.address), ACPI_LODWORD (obj_desc->region.address),
		obj_desc->region.length));

	/* Now the address and length are valid for this opregion */

	obj_desc->region.flags |= AOPOBJ_DATA_VALID;

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_eval_data_object_operands
 *
 * PARAMETERS:  Walk_state      - Current walk
 *              Op              - A valid Data_object Op object
 *              Obj_desc        - Data_object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the operands and complete the following data objec types:
 *              Buffer
 *              Package
 *
 ****************************************************************************/

acpi_status
acpi_ds_eval_data_object_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_operand_object     *obj_desc)
{
	acpi_status             status;
	acpi_operand_object     *arg_desc;
	u32                     length;


	ACPI_FUNCTION_TRACE ("Ds_eval_data_object_operands");


	/* The first operand (for all of these data objects) is the length */

	status = acpi_ds_create_operand (walk_state, op->common.value.arg, 1);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_ex_resolve_operands (walk_state->opcode,
			  &(walk_state->operands [walk_state->num_operands -1]),
			  walk_state);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Extract length operand */

	arg_desc = walk_state->operands [walk_state->num_operands - 1];
	length = (u32) arg_desc->integer.value;

	/* Cleanup for length operand */

	status = acpi_ds_obj_stack_pop (1, walk_state);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	acpi_ut_remove_reference (arg_desc);

	/*
	 * Create the actual data object
	 */
	switch (op->common.aml_opcode) {
	case AML_BUFFER_OP:

		status = acpi_ds_build_internal_buffer_obj (walk_state, op, length, &obj_desc);
		break;

	case AML_PACKAGE_OP:
	case AML_VAR_PACKAGE_OP:

		status = acpi_ds_build_internal_package_obj (walk_state, op, length, &obj_desc);
		break;

	default:
		return_ACPI_STATUS (AE_AML_BAD_OPCODE);
	}

	if (ACPI_SUCCESS (status)) {
		/*
		 * Return the object in the Walk_state, unless the parent is a package --
		 * in this case, the return object will be stored in the parse tree
		 * for the package.
		 */
		if ((!op->common.parent) ||
			((op->common.parent->common.aml_opcode != AML_PACKAGE_OP) &&
			 (op->common.parent->common.aml_opcode != AML_VAR_PACKAGE_OP) &&
			 (op->common.parent->common.aml_opcode != AML_NAME_OP))) {
			walk_state->result_obj = obj_desc;
		}
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_begin_control_op
 *
 * PARAMETERS:  Walk_list       - The list that owns the walk stack
 *              Op              - The control Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handles all control ops encountered during control method
 *              execution.
 *
 ******************************************************************************/

acpi_status
acpi_ds_exec_begin_control_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_status             status = AE_OK;
	acpi_generic_state      *control_state;


	ACPI_FUNCTION_NAME ("Ds_exec_begin_control_op");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p Opcode=%2.2X State=%p\n", op,
		op->common.aml_opcode, walk_state));

	switch (op->common.aml_opcode) {
	case AML_IF_OP:
	case AML_WHILE_OP:

		/*
		 * IF/WHILE: Create a new control state to manage these
		 * constructs. We need to manage these as a stack, in order
		 * to handle nesting.
		 */
		control_state = acpi_ut_create_control_state ();
		if (!control_state) {
			status = AE_NO_MEMORY;
			break;
		}
		/*
		 * Save a pointer to the predicate for multiple executions
		 * of a loop
		 */
		control_state->control.aml_predicate_start = walk_state->parser_state.aml - 1;
		control_state->control.package_end = walk_state->parser_state.pkg_end;
		control_state->control.opcode = op->common.aml_opcode;


		/* Push the control state on this walk's control stack */

		acpi_ut_push_generic_state (&walk_state->control_state, control_state);
		break;

	case AML_ELSE_OP:

		/* Predicate is in the state object */
		/* If predicate is true, the IF was executed, ignore ELSE part */

		if (walk_state->last_predicate) {
			status = AE_CTRL_TRUE;
		}

		break;

	case AML_RETURN_OP:

		break;

	default:
		break;
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_end_control_op
 *
 * PARAMETERS:  Walk_list       - The list that owns the walk stack
 *              Op              - The control Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handles all control ops encountered during control method
 *              execution.
 *
 ******************************************************************************/

acpi_status
acpi_ds_exec_end_control_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_status             status = AE_OK;
	acpi_generic_state      *control_state;


	ACPI_FUNCTION_NAME ("Ds_exec_end_control_op");


	switch (op->common.aml_opcode) {
	case AML_IF_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "[IF_OP] Op=%p\n", op));

		/*
		 * Save the result of the predicate in case there is an
		 * ELSE to come
		 */
		walk_state->last_predicate =
			(u8) walk_state->control_state->common.value;

		/*
		 * Pop the control state that was created at the start
		 * of the IF and free it
		 */
		control_state = acpi_ut_pop_generic_state (&walk_state->control_state);
		acpi_ut_delete_generic_state (control_state);
		break;


	case AML_ELSE_OP:

		break;


	case AML_WHILE_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "[WHILE_OP] Op=%p\n", op));

		if (walk_state->control_state->common.value) {
			/* Predicate was true, go back and evaluate it again! */

			status = AE_CTRL_PENDING;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "[WHILE_OP] termination! Op=%p\n", op));

		/* Pop this control state and free it */

		control_state = acpi_ut_pop_generic_state (&walk_state->control_state);

		walk_state->aml_last_while = control_state->control.aml_predicate_start;
		acpi_ut_delete_generic_state (control_state);
		break;


	case AML_RETURN_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"[RETURN_OP] Op=%p Arg=%p\n",op, op->common.value.arg));

		/*
		 * One optional operand -- the return value
		 * It can be either an immediate operand or a result that
		 * has been bubbled up the tree
		 */
		if (op->common.value.arg) {
			/* Return statement has an immediate operand */

			status = acpi_ds_create_operands (walk_state, op->common.value.arg);
			if (ACPI_FAILURE (status)) {
				return (status);
			}

			/*
			 * If value being returned is a Reference (such as
			 * an arg or local), resolve it now because it may
			 * cease to exist at the end of the method.
			 */
			status = acpi_ex_resolve_to_value (&walk_state->operands [0], walk_state);
			if (ACPI_FAILURE (status)) {
				return (status);
			}

			/*
			 * Get the return value and save as the last result
			 * value.  This is the only place where Walk_state->Return_desc
			 * is set to anything other than zero!
			 */
			walk_state->return_desc = walk_state->operands[0];
		}
		else if ((walk_state->results) &&
				 (walk_state->results->results.num_results > 0)) {
			/*
			 * The return value has come from a previous calculation.
			 *
			 * If value being returned is a Reference (such as
			 * an arg or local), resolve it now because it may
			 * cease to exist at the end of the method.
			 *
			 * Allow references created by the Index operator to return unchanged.
			 */
			if ((ACPI_GET_DESCRIPTOR_TYPE (walk_state->results->results.obj_desc[0]) == ACPI_DESC_TYPE_OPERAND) &&
				(ACPI_GET_OBJECT_TYPE (walk_state->results->results.obj_desc [0]) == INTERNAL_TYPE_REFERENCE) &&
				((walk_state->results->results.obj_desc [0])->reference.opcode != AML_INDEX_OP)) {
				status = acpi_ex_resolve_to_value (&walk_state->results->results.obj_desc [0], walk_state);
				if (ACPI_FAILURE (status)) {
					return (status);
				}
			}

			walk_state->return_desc = walk_state->results->results.obj_desc [0];
		}
		else {
			/* No return operand */

			if (walk_state->num_operands) {
				acpi_ut_remove_reference (walk_state->operands [0]);
			}

			walk_state->operands [0]    = NULL;
			walk_state->num_operands    = 0;
			walk_state->return_desc     = NULL;
		}


		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Completed RETURN_OP State=%p, Ret_val=%p\n",
			walk_state, walk_state->return_desc));

		/* End the control method execution right now */

		status = AE_CTRL_TERMINATE;
		break;


	case AML_NOOP_OP:

		/* Just do nothing! */
		break;


	case AML_BREAK_POINT_OP:

		/* Call up to the OS service layer to handle this */

		status = acpi_os_signal (ACPI_SIGNAL_BREAKPOINT, "Executed AML Breakpoint opcode");

		/* If and when it returns, all done. */

		break;


	case AML_BREAK_OP:
	case AML_CONTINUE_OP: /* ACPI 2.0 */


		/* Pop and delete control states until we find a while */

		while (walk_state->control_state &&
				(walk_state->control_state->control.opcode != AML_WHILE_OP)) {
			control_state = acpi_ut_pop_generic_state (&walk_state->control_state);
			acpi_ut_delete_generic_state (control_state);
		}

		/* No while found? */

		if (!walk_state->control_state) {
			return (AE_AML_NO_WHILE);
		}

		/* Was: Walk_state->Aml_last_while = Walk_state->Control_state->Control.Aml_predicate_start; */

		walk_state->aml_last_while = walk_state->control_state->control.package_end;

		/* Return status depending on opcode */

		if (op->common.aml_opcode == AML_BREAK_OP) {
			status = AE_CTRL_BREAK;
		}
		else {
			status = AE_CTRL_CONTINUE;
		}
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown control opcode=%X Op=%p\n",
			op->common.aml_opcode, op));

		status = AE_AML_BAD_OPCODE;
		break;
	}

	return (status);
}

