/******************************************************************************
 *
 * Module Name: dsopcode - Dispatcher Op Region support and handling of
 *                         "control" opcodes
 *              $Revision: 56 $
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
#include "acevents.h"
#include "actables.h"

#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dsopcode")


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
	acpi_parse_object       *op;
	acpi_parse_object       *field_op;
	acpi_status             status;
	acpi_table_desc         *table_desc;
	acpi_walk_state         *walk_state;


	FUNCTION_TRACE_PTR ("Ds_get_buffer_field_arguments", obj_desc);


	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS (AE_OK);
	}


	/* Get the AML pointer (method object) and Buffer_field node */

	extra_desc = obj_desc->buffer_field.extra;
	node = obj_desc->buffer_field.node;

	DEBUG_EXEC(acpi_ut_display_init_pathname (node, " [Field]"));
	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[%4.4s] Buffer_field JIT Init\n",
		(char*)&node->name));


	/*
	 * Allocate a new parser op to be the root of the parsed
	 * Op_region tree
	 */
	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	/* Save the Node for use in Acpi_ps_parse_aml */

	op->node = acpi_ns_get_parent_object (node);

	/* Get a handle to the parent ACPI table */

	status = acpi_tb_handle_to_object (node->owner_id, &table_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT,
			   NULL, NULL, NULL);
	if (!walk_state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk (walk_state, op, NULL, extra_desc->extra.aml_start,
			  extra_desc->extra.aml_length, NULL, NULL, 1);
	if (ACPI_FAILURE (status)) {
		/* TBD: delete walk state */
		return_ACPI_STATUS (status);
	}

	/* TBD: No Walk flags?? */

	walk_state->parse_flags = 0;

	/* Pass1: Parse the entire Buffer_field declaration */

	status = acpi_ps_parse_aml (walk_state);
	if (ACPI_FAILURE (status)) {
		acpi_ps_delete_parse_tree (op);
		return_ACPI_STATUS (status);
	}

	/* Get and init the actual Field_unit Op created above */

	field_op = op->value.arg;
	op->node = node;


	field_op = op->value.arg;
	field_op->node = node;
	acpi_ps_delete_parse_tree (op);

	/* Evaluate the address and length arguments for the Op_region */

	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	op->node = acpi_ns_get_parent_object (node);

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT,
			   NULL, NULL, NULL);
	if (!walk_state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk (walk_state, op, NULL, extra_desc->extra.aml_start,
			  extra_desc->extra.aml_length, NULL, NULL, 3);
	if (ACPI_FAILURE (status)) {
		/* TBD: delete walk state */
		return_ACPI_STATUS (status);
	}

	status = acpi_ps_parse_aml (walk_state);
	acpi_ps_delete_parse_tree (op);

	/*
	 * The pseudo-method object is no longer needed since the region is
	 * now initialized
	 */
	acpi_ut_remove_reference (obj_desc->buffer_field.extra);
	obj_desc->buffer_field.extra = NULL;

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
	acpi_operand_object     *extra_desc = NULL;
	acpi_namespace_node     *node;
	acpi_parse_object       *op;
	acpi_parse_object       *region_op;
	acpi_status             status;
	acpi_table_desc         *table_desc;
	acpi_walk_state         *walk_state;


	FUNCTION_TRACE_PTR ("Ds_get_region_arguments", obj_desc);


	if (obj_desc->region.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS (AE_OK);
	}


	/* Get the AML pointer (method object) and region node */

	extra_desc = obj_desc->region.extra;
	node = obj_desc->region.node;

	DEBUG_EXEC(acpi_ut_display_init_pathname (node, " [Operation Region]"));

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[%4.4s] Op_region Init at AML %p\n",
		(char*)&node->name, extra_desc->extra.aml_start));

	/*
	 * Allocate a new parser op to be the root of the parsed
	 * Op_region tree
	 */
	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	/* Save the Node for use in Acpi_ps_parse_aml */

	op->node = acpi_ns_get_parent_object (node);

	/* Get a handle to the parent ACPI table */

	status = acpi_tb_handle_to_object (node->owner_id, &table_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT,
			   op, NULL, NULL);
	if (!walk_state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk (walk_state, op, NULL, extra_desc->extra.aml_start,
			  extra_desc->extra.aml_length, NULL, NULL, 1);
	if (ACPI_FAILURE (status)) {
		/* TBD: delete walk state */
		return_ACPI_STATUS (status);
	}

	/* TBD: No Walk flags?? */

	walk_state->parse_flags = 0;

	/* Parse the entire Op_region declaration, creating a parse tree */

	status = acpi_ps_parse_aml (walk_state);
	if (ACPI_FAILURE (status)) {
		acpi_ps_delete_parse_tree (op);
		return_ACPI_STATUS (status);
	}

	/* Get and init the actual Region_op created above */

	region_op = op->value.arg;
	op->node = node;


	region_op = op->value.arg;
	region_op->node = node;
	acpi_ps_delete_parse_tree (op);

	/* Evaluate the address and length arguments for the Op_region */

	op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	op->node = acpi_ns_get_parent_object (node);

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT,
			   op, NULL, NULL);
	if (!walk_state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk (walk_state, op, NULL, extra_desc->extra.aml_start,
			  extra_desc->extra.aml_length, NULL, NULL, 3);
	if (ACPI_FAILURE (status)) {
		/* TBD: delete walk state */
		return_ACPI_STATUS (status);
	}

	status = acpi_ps_parse_aml (walk_state);
	acpi_ps_delete_parse_tree (op);

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
 * DESCRIPTION:
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
 * FUNCTION:    Acpi_ds_eval_buffer_field_operands
 *
 * PARAMETERS:  Op              - A valid Buffer_field Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get Buffer_field Buffer and Index
 *              Called from Acpi_ds_exec_end_op during Buffer_field parse tree walk
 *
 * ACPI SPECIFICATION REFERENCES:
 *  Each of the Buffer Field opcodes is defined as specified in in-line
 *  comments below. For each one, use the following definitions.
 *
 *  Def_bit_field   :=  Bit_field_op    Src_buf Bit_idx Destination
 *  Def_byte_field  :=  Byte_field_op   Src_buf Byte_idx Destination
 *  Def_create_field := Create_field_op Src_buf Bit_idx Num_bits Name_string
 *  Def_dWord_field :=  DWord_field_op  Src_buf Byte_idx Destination
 *  Def_word_field  :=  Word_field_op   Src_buf Byte_idx Destination
 *  Bit_index       :=  Term_arg=>Integer
 *  Byte_index      :=  Term_arg=>Integer
 *  Destination     :=  Name_string
 *  Num_bits        :=  Term_arg=>Integer
 *  Source_buf      :=  Term_arg=>Buffer
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
	u32                     offset;
	u32                     bit_offset;
	u32                     bit_count;
	u8                      field_flags;
	acpi_operand_object     *res_desc = NULL;
	acpi_operand_object     *cnt_desc = NULL;
	acpi_operand_object     *off_desc = NULL;
	acpi_operand_object     *src_desc = NULL;


	FUNCTION_TRACE_PTR ("Ds_eval_buffer_field_operands", op);


	/*
	 * This is where we evaluate the address and length fields of the
	 * Create_xxx_field declaration
	 */
	node =  op->node;

	/* Next_op points to the op that holds the Buffer */

	next_op = op->value.arg;

	/* Acpi_evaluate/create the address and length operands */

	status = acpi_ds_create_operands (walk_state, next_op);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	obj_desc = acpi_ns_get_attached_object (node);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}


	/* Resolve the operands */

	status = acpi_ex_resolve_operands (op->opcode, WALK_OPERANDS, walk_state);
	DUMP_OPERANDS (WALK_OPERANDS, IMODE_EXECUTE, acpi_ps_get_opcode_name (op->opcode),
			  walk_state->num_operands, "after Acpi_ex_resolve_operands");

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "(%s) bad operand(s) (%X)\n",
			acpi_ps_get_opcode_name (op->opcode), status));

		goto cleanup;
	}

	/* Get the operands */

	if (AML_CREATE_FIELD_OP == op->opcode) {
		res_desc = walk_state->operands[3];
		cnt_desc = walk_state->operands[2];
	}
	else {
		res_desc = walk_state->operands[2];
	}

	off_desc = walk_state->operands[1];
	src_desc = walk_state->operands[0];


	offset = (u32) off_desc->integer.value;

	/*
	 * If Res_desc is a Name, it will be a direct name pointer after
	 * Acpi_ex_resolve_operands()
	 */
	if (!VALID_DESCRIPTOR_TYPE (res_desc, ACPI_DESC_TYPE_NAMED)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "(%s) destination must be a Node\n",
			acpi_ps_get_opcode_name (op->opcode)));

		status = AE_AML_OPERAND_TYPE;
		goto cleanup;
	}

	/*
	 * Setup the Bit offsets and counts, according to the opcode
	 */
	switch (op->opcode) {

	/* Def_create_field */

	case AML_CREATE_FIELD_OP:

		/* Offset is in bits, count is in bits */

		bit_offset  = offset;
		bit_count   = (u32) cnt_desc->integer.value;
		field_flags = ACCESS_BYTE_ACC;
		break;


	/* Def_create_bit_field */

	case AML_CREATE_BIT_FIELD_OP:

		/* Offset is in bits, Field is one bit */

		bit_offset  = offset;
		bit_count   = 1;
		field_flags = ACCESS_BYTE_ACC;
		break;


	/* Def_create_byte_field */

	case AML_CREATE_BYTE_FIELD_OP:

		/* Offset is in bytes, field is one byte */

		bit_offset  = 8 * offset;
		bit_count   = 8;
		field_flags = ACCESS_BYTE_ACC;
		break;


	/* Def_create_word_field */

	case AML_CREATE_WORD_FIELD_OP:

		/* Offset is in bytes, field is one word */

		bit_offset  = 8 * offset;
		bit_count   = 16;
		field_flags = ACCESS_WORD_ACC;
		break;


	/* Def_create_dWord_field */

	case AML_CREATE_DWORD_FIELD_OP:

		/* Offset is in bytes, field is one dword */

		bit_offset  = 8 * offset;
		bit_count   = 32;
		field_flags = ACCESS_DWORD_ACC;
		break;


	/* Def_create_qWord_field */

	case AML_CREATE_QWORD_FIELD_OP:

		/* Offset is in bytes, field is one qword */

		bit_offset  = 8 * offset;
		bit_count   = 64;
		field_flags = ACCESS_QWORD_ACC;
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Internal error - unknown field creation opcode %02x\n",
			op->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}


	/*
	 * Setup field according to the object type
	 */
	switch (src_desc->common.type) {

	/* Source_buff :=  Term_arg=>Buffer */

	case ACPI_TYPE_BUFFER:

		if ((bit_offset + bit_count) >
			(8 * (u32) src_desc->buffer.length)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Field size %d exceeds Buffer size %d (bits)\n",
				 bit_offset + bit_count, 8 * (u32) src_desc->buffer.length));
			status = AE_AML_BUFFER_LIMIT;
			goto cleanup;
		}


		/*
		 * Initialize areas of the field object that are common to all fields
		 * For Field_flags, use LOCK_RULE = 0 (NO_LOCK), UPDATE_RULE = 0 (UPDATE_PRESERVE)
		 */
		status = acpi_ex_prep_common_field_object (obj_desc, field_flags,
				  bit_offset, bit_count);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		obj_desc->buffer_field.buffer_obj = src_desc;

		/* Reference count for Src_desc inherits Obj_desc count */

		src_desc->common.reference_count = (u16) (src_desc->common.reference_count +
				  obj_desc->common.reference_count);

		break;


	/* Improper object type */

	default:

		if ((src_desc->common.type > (u8) INTERNAL_TYPE_REFERENCE) || !acpi_ut_valid_object_type (src_desc->common.type)) /* TBD: This line MUST be a single line until Acpi_src can handle it (block deletion) */ {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Tried to create field in invalid object type %X\n",
				src_desc->common.type));
		}

		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Tried to create field in improper object type - %s\n",
				acpi_ut_get_type_name (src_desc->common.type)));
		}

		status = AE_AML_OPERAND_TYPE;
		goto cleanup;
	}


	if (AML_CREATE_FIELD_OP == op->opcode) {
		/* Delete object descriptor unique to Create_field */

		acpi_ut_remove_reference (cnt_desc);
		cnt_desc = NULL;
	}


cleanup:

	/* Always delete the operands */

	acpi_ut_remove_reference (off_desc);
	acpi_ut_remove_reference (src_desc);

	if (AML_CREATE_FIELD_OP == op->opcode) {
		acpi_ut_remove_reference (cnt_desc);
	}

	/* On failure, delete the result descriptor */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (res_desc); /* Result descriptor */
	}

	else {
		/* Now the address and length are valid for this Buffer_field */

		obj_desc->buffer_field.flags |= AOPOBJ_DATA_VALID;
	}

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_eval_region_operands
 *
 * PARAMETERS:  Op              - A valid region Op object
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


	FUNCTION_TRACE_PTR ("Ds_eval_region_operands", op);


	/*
	 * This is where we evaluate the address and length fields of the Op_region declaration
	 */
	node =  op->node;

	/* Next_op points to the op that holds the Space_iD */

	next_op = op->value.arg;

	/* Next_op points to address op */

	next_op = next_op->next;

	/* Acpi_evaluate/create the address and length operands */

	status = acpi_ds_create_operands (walk_state, next_op);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Resolve the length and address operands to numbers */

	status = acpi_ex_resolve_operands (op->opcode, WALK_OPERANDS, walk_state);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	DUMP_OPERANDS (WALK_OPERANDS, IMODE_EXECUTE,
			  acpi_ps_get_opcode_name (op->opcode),
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
		obj_desc, HIDWORD(obj_desc->region.address), LODWORD(obj_desc->region.address),
		obj_desc->region.length));

	/* Now the address and length are valid for this opregion */

	obj_desc->region.flags |= AOPOBJ_DATA_VALID;

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


	PROC_NAME ("Ds_exec_begin_control_op");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p Opcode=%2.2X State=%p\n", op,
		op->opcode, walk_state));

	switch (op->opcode) {
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

		acpi_ut_push_generic_state (&walk_state->control_state, control_state);

		/*
		 * Save a pointer to the predicate for multiple executions
		 * of a loop
		 */
		walk_state->control_state->control.aml_predicate_start =
				 walk_state->parser_state.aml - 1;
				 /* TBD: can this be removed? */
				 /*Acpi_ps_pkg_length_encoding_size (GET8 (Walk_state->Parser_state->Aml));*/
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
 *
 ******************************************************************************/

acpi_status
acpi_ds_exec_end_control_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_status             status = AE_OK;
	acpi_generic_state      *control_state;


	PROC_NAME ("Ds_exec_end_control_op");


	switch (op->opcode) {
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
			"[RETURN_OP] Op=%p Arg=%p\n",op, op->value.arg));

		/*
		 * One optional operand -- the return value
		 * It can be either an immediate operand or a result that
		 * has been bubbled up the tree
		 */
		if (op->value.arg) {
			/* Return statement has an immediate operand */

			status = acpi_ds_create_operands (walk_state, op->value.arg);
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
			if (VALID_DESCRIPTOR_TYPE (walk_state->results->results.obj_desc [0], ACPI_DESC_TYPE_INTERNAL) &&
				((walk_state->results->results.obj_desc [0])->common.type == INTERNAL_TYPE_REFERENCE) &&
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

		acpi_os_signal (ACPI_SIGNAL_BREAKPOINT, "Executed AML Breakpoint opcode");

		/* If and when it returns, all done. */

		break;


	case AML_BREAK_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Break to end of current package, Op=%p\n", op));

		/* TBD: update behavior for ACPI 2.0 */

		/*
		 * As per the ACPI specification:
		 *      "The break operation causes the current package
		 *          execution to complete"
		 *      "Break -- Stop executing the current code package
		 *          at this point"
		 *
		 * Returning AE_FALSE here will cause termination of
		 * the current package, and execution will continue one
		 * level up, starting with the completion of the parent Op.
		 */
		status = AE_CTRL_FALSE;
		break;


	case AML_CONTINUE_OP: /* ACPI 2.0 */

		status = AE_NOT_IMPLEMENTED;
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown control opcode=%X Op=%p\n",
			op->opcode, op));

		status = AE_AML_BAD_OPCODE;
		break;
	}


	return (status);
}

