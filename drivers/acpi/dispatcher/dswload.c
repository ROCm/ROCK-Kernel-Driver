/******************************************************************************
 *
 * Module Name: dswload - Dispatcher namespace load callbacks
 *              $Revision: 71 $
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
	 ACPI_MODULE_NAME    ("dswload")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_init_callbacks
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Pass_number     - 1, 2, or 3
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Init walk state callbacks
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_callbacks (
	acpi_walk_state         *walk_state,
	u32                     pass_number)
{

	switch (pass_number) {
	case 1:
		walk_state->parse_flags       = ACPI_PARSE_LOAD_PASS1 | ACPI_PARSE_DELETE_TREE;
		walk_state->descending_callback = acpi_ds_load1_begin_op;
		walk_state->ascending_callback = acpi_ds_load1_end_op;
		break;

	case 2:
		walk_state->parse_flags       = ACPI_PARSE_LOAD_PASS1 | ACPI_PARSE_DELETE_TREE;
		walk_state->descending_callback = acpi_ds_load2_begin_op;
		walk_state->ascending_callback = acpi_ds_load2_end_op;
		break;

	case 3:
#ifndef ACPI_NO_METHOD_EXECUTION
		walk_state->parse_flags      |= ACPI_PARSE_EXECUTE  | ACPI_PARSE_DELETE_TREE;
		walk_state->descending_callback = acpi_ds_exec_begin_op;
		walk_state->ascending_callback = acpi_ds_exec_end_op;
#endif
		break;

	default:
		return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_load1_begin_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been reached in the
 *                                walk;  Arguments have not been evaluated yet.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the loading of ACPI tables.
 *
 ******************************************************************************/

acpi_status
acpi_ds_load1_begin_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op)
{
	acpi_parse_object       *op;
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_object_type        object_type;
	NATIVE_CHAR             *path;
	u32                     flags;


	ACPI_FUNCTION_NAME ("Ds_load1_begin_op");


	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));

	/* We are only interested in opcodes that have an associated name */

	if (op) {
		if (!(walk_state->op_info->flags & AML_NAMED)) {
#if 0
			if ((walk_state->op_info->class == AML_CLASS_EXECUTE) ||
				(walk_state->op_info->class == AML_CLASS_CONTROL)) {
				acpi_os_printf ("\n\n***EXECUTABLE OPCODE %s***\n\n", walk_state->op_info->name);
				*out_op = op;
				return (AE_CTRL_SKIP);
			}
#endif
			*out_op = op;
			return (AE_OK);
		}

		/* Check if this object has already been installed in the namespace */

		if (op->common.node) {
			*out_op = op;
			return (AE_OK);
		}
	}

	path = acpi_ps_get_next_namestring (&walk_state->parser_state);

	/* Map the raw opcode into an internal object type */

	object_type = walk_state->op_info->object_type;

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"State=%p Op=%p [%s] ", walk_state, op, acpi_ut_get_type_name (object_type)));

	/*
	 * Setup the search flags.
	 *
	 * Since we are entering a name into the namespace, we do not want to
	 *    enable the search-to-root upsearch.
	 *
	 * There are only two conditions where it is acceptable that the name
	 *    already exists:
	 *    1) the Scope() operator can reopen a scoping object that was
	 *       previously defined (Scope, Method, Device, etc.)
	 *    2) Whenever we are parsing a deferred opcode (Op_region, Buffer,
	 *       Buffer_field, or Package), the name of the object is already
	 *       in the namespace.
	 */
	flags = ACPI_NS_NO_UPSEARCH;
	if ((walk_state->opcode != AML_SCOPE_OP) &&
		(!(walk_state->parse_flags & ACPI_PARSE_DEFERRED_OP))) {
		flags |= ACPI_NS_ERROR_IF_FOUND;
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DISPATCH, "Cannot already exist\n"));
	}
	else {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DISPATCH, "Both Find or Create allowed\n"));
	}

	/*
	 * Enter the named type into the internal namespace.  We enter the name
	 * as we go downward in the parse tree.  Any necessary subobjects that involve
	 * arguments to the opcode must be created as we go back up the parse tree later.
	 */
	status = acpi_ns_lookup (walk_state->scope_info, path, object_type,
			  ACPI_IMODE_LOAD_PASS1, flags, walk_state, &(node));
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/*
	 * For the scope op, we must check to make sure that the target is
	 * one of the opcodes that actually opens a scope
	 */
	if (walk_state->opcode == AML_SCOPE_OP) {
		switch (node->type) {
		case ACPI_TYPE_ANY:         /* Scope nodes are untyped (ANY) */
		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_METHOD:
		case ACPI_TYPE_POWER:
		case ACPI_TYPE_PROCESSOR:
		case ACPI_TYPE_THERMAL:

			/* These are acceptable types */
			break;

		default:

			/* All other types are an error */

			ACPI_REPORT_ERROR (("Invalid type (%s) for target of Scope operator [%4.4s]\n",
				acpi_ut_get_type_name (node->type), path));

			return (AE_AML_OPERAND_TYPE);
		}
	}

	if (!op) {
		/* Create a new op */

		op = acpi_ps_alloc_op (walk_state->opcode);
		if (!op) {
			return (AE_NO_MEMORY);
		}
	}

	/* Initialize */

	op->named.name = node->name.integer;

#if (defined (ACPI_NO_METHOD_EXECUTION) || defined (ACPI_CONSTANT_EVAL_ONLY))
	op->named.path = (u8 *) path;
#endif


	/*
	 * Put the Node in the "op" object that the parser uses, so we
	 * can get it again quickly when this scope is closed
	 */
	op->common.node = node;
	acpi_ps_append_arg (acpi_ps_get_parent_scope (&walk_state->parser_state), op);

	*out_op = op;
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_load1_end_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been completed in the
 *                                walk;  Arguments have now been evaluated.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the loading of the namespace,
 *              both control methods and everything else.
 *
 ******************************************************************************/

acpi_status
acpi_ds_load1_end_op (
	acpi_walk_state         *walk_state)
{
	acpi_parse_object       *op;
	acpi_object_type        object_type;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_NAME ("Ds_load1_end_op");


	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));

	/* We are only interested in opcodes that have an associated name */

	if (!(walk_state->op_info->flags & (AML_NAMED | AML_FIELD))) {
		return (AE_OK);
	}

	/* Get the object type to determine if we should pop the scope */

	object_type = walk_state->op_info->object_type;

#ifndef ACPI_NO_METHOD_EXECUTION
	if (walk_state->op_info->flags & AML_FIELD) {
		if (walk_state->opcode == AML_FIELD_OP         ||
			walk_state->opcode == AML_BANK_FIELD_OP    ||
			walk_state->opcode == AML_INDEX_FIELD_OP) {
			status = acpi_ds_init_field_objects (op, walk_state);
		}
		return (status);
	}


	if (op->common.aml_opcode == AML_REGION_OP) {
		status = acpi_ex_create_region (op->named.data, op->named.length,
				   (ACPI_ADR_SPACE_TYPE) ((op->common.value.arg)->common.value.integer), walk_state);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}
#endif

	if (op->common.aml_opcode == AML_NAME_OP) {
		/* For Name opcode, get the object type from the argument */

		if (op->common.value.arg) {
			object_type = (acpi_ps_get_opcode_info ((op->common.value.arg)->common.aml_opcode))->object_type;
			op->common.node->type = (u8) object_type;
		}
	}

	/* Pop the scope stack */

	if (acpi_ns_opens_scope (object_type)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "(%s): Popping scope for Op %p\n",
			acpi_ut_get_type_name (object_type), op));

		status = acpi_ds_scope_stack_pop (walk_state);
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_load2_begin_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been reached in the
 *                                walk;  Arguments have not been evaluated yet.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the loading of ACPI tables.
 *
 ******************************************************************************/

acpi_status
acpi_ds_load2_begin_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op)
{
	acpi_parse_object       *op;
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_object_type        object_type;
	NATIVE_CHAR             *buffer_ptr;


	ACPI_FUNCTION_TRACE ("Ds_load2_begin_op");


	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));

	if (op) {
		/* We only care about Namespace opcodes here */

		if ((!(walk_state->op_info->flags & AML_NSOPCODE) && (walk_state->opcode != AML_INT_NAMEPATH_OP)) ||
			(!(walk_state->op_info->flags & AML_NAMED))) {
			return_ACPI_STATUS (AE_OK);
		}

		/*
		 * Get the name we are going to enter or lookup in the namespace
		 */
		if (walk_state->opcode == AML_INT_NAMEPATH_OP) {
			/* For Namepath op, get the path string */

			buffer_ptr = op->common.value.string;
			if (!buffer_ptr) {
				/* No name, just exit */

				return_ACPI_STATUS (AE_OK);
			}
		}
		else {
			/* Get name from the op */

			buffer_ptr = (NATIVE_CHAR *) &op->named.name;
		}
	}
	else {
		/* Get the namestring from the raw AML */

		buffer_ptr = acpi_ps_get_next_namestring (&walk_state->parser_state);
	}

	/* Map the opcode into an internal object type */

	object_type = walk_state->op_info->object_type;

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"State=%p Op=%p Type=%X\n", walk_state, op, object_type));


	if (walk_state->opcode == AML_FIELD_OP         ||
		walk_state->opcode == AML_BANK_FIELD_OP    ||
		walk_state->opcode == AML_INDEX_FIELD_OP) {
		node = NULL;
		status = AE_OK;
	}
	else if (walk_state->opcode == AML_INT_NAMEPATH_OP) {
		/*
		 * The Name_path is an object reference to an existing object. Don't enter the
		 * name into the namespace, but look it up for use later
		 */
		status = acpi_ns_lookup (walk_state->scope_info, buffer_ptr, object_type,
				  ACPI_IMODE_EXECUTE, ACPI_NS_SEARCH_PARENT, walk_state, &(node));
	}
	else {
		/* All other opcodes */

		if (op && op->common.node) {
			/* This op/node was previously entered into the namespace */

			node = op->common.node;

			if (acpi_ns_opens_scope (object_type)) {
				status = acpi_ds_scope_stack_push (node, object_type, walk_state);
				if (ACPI_FAILURE (status)) {
					return_ACPI_STATUS (status);
				}

			}
			return_ACPI_STATUS (AE_OK);
		}

		/*
		 * Enter the named type into the internal namespace.  We enter the name
		 * as we go downward in the parse tree.  Any necessary subobjects that involve
		 * arguments to the opcode must be created as we go back up the parse tree later.
		 */
		status = acpi_ns_lookup (walk_state->scope_info, buffer_ptr, object_type,
				  ACPI_IMODE_EXECUTE, ACPI_NS_NO_UPSEARCH, walk_state, &(node));
	}

	if (ACPI_SUCCESS (status)) {
		if (!op) {
			/* Create a new op */

			op = acpi_ps_alloc_op (walk_state->opcode);
			if (!op) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			/* Initialize the new op */

			if (node) {
				op->named.name = node->name.integer;
			}
			if (out_op) {
				*out_op = op;
			}
		}

		/*
		 * Put the Node in the "op" object that the parser uses, so we
		 * can get it again quickly when this scope is closed
		 */
		op->common.node = node;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_load2_end_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been completed in the
 *                                walk;  Arguments have now been evaluated.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the loading of the namespace,
 *              both control methods and everything else.
 *
 ******************************************************************************/

acpi_status
acpi_ds_load2_end_op (
	acpi_walk_state         *walk_state)
{
	acpi_parse_object       *op;
	acpi_status             status = AE_OK;
	acpi_object_type        object_type;
	acpi_namespace_node     *node;
	acpi_parse_object       *arg;
	acpi_namespace_node     *new_node;
#ifndef ACPI_NO_METHOD_EXECUTION
	u32                     i;
#endif


	ACPI_FUNCTION_TRACE ("Ds_load2_end_op");

	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Opcode [%s] Op %p State %p\n",
			walk_state->op_info->name, op, walk_state));

	/* Only interested in opcodes that have namespace objects */

	if (!(walk_state->op_info->flags & AML_NSOBJECT)) {
		return_ACPI_STATUS (AE_OK);
	}

	if (op->common.aml_opcode == AML_SCOPE_OP) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Ending scope Op=%p State=%p\n", op, walk_state));
	}


	object_type = walk_state->op_info->object_type;

	/*
	 * Get the Node/name from the earlier lookup
	 * (It was saved in the *op structure)
	 */
	node = op->common.node;

	/*
	 * Put the Node on the object stack (Contains the ACPI Name of
	 * this object)
	 */
	walk_state->operands[0] = (void *) node;
	walk_state->num_operands = 1;

	/* Pop the scope stack */

	if (acpi_ns_opens_scope (object_type) && (op->common.aml_opcode != AML_INT_METHODCALL_OP)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "(%s) Popping scope for Op %p\n",
			acpi_ut_get_type_name (object_type), op));

		status = acpi_ds_scope_stack_pop (walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}


	/*
	 * Named operations are as follows:
	 *
	 * AML_ALIAS
	 * AML_BANKFIELD
	 * AML_CREATEBITFIELD
	 * AML_CREATEBYTEFIELD
	 * AML_CREATEDWORDFIELD
	 * AML_CREATEFIELD
	 * AML_CREATEQWORDFIELD
	 * AML_CREATEWORDFIELD
	 * AML_DATA_REGION
	 * AML_DEVICE
	 * AML_EVENT
	 * AML_FIELD
	 * AML_INDEXFIELD
	 * AML_METHOD
	 * AML_METHODCALL
	 * AML_MUTEX
	 * AML_NAME
	 * AML_NAMEDFIELD
	 * AML_OPREGION
	 * AML_POWERRES
	 * AML_PROCESSOR
	 * AML_SCOPE
	 * AML_THERMALZONE
	 */

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"Create-Load [%s] State=%p Op=%p Named_obj=%p\n",
		acpi_ps_get_opcode_name (op->common.aml_opcode), walk_state, op, node));

	/* Decode the opcode */

	arg = op->common.value.arg;

	switch (walk_state->op_info->type) {
#ifndef ACPI_NO_METHOD_EXECUTION

	case AML_TYPE_CREATE_FIELD:

		/*
		 * Create the field object, but the field buffer and index must
		 * be evaluated later during the execution phase
		 */
		status = acpi_ds_create_buffer_field (op, walk_state);
		break;


	 case AML_TYPE_NAMED_FIELD:

		switch (op->common.aml_opcode) {
		case AML_INDEX_FIELD_OP:

			status = acpi_ds_create_index_field (op, (acpi_handle) arg->common.node,
					   walk_state);
			break;

		case AML_BANK_FIELD_OP:

			status = acpi_ds_create_bank_field (op, arg->common.node, walk_state);
			break;

		case AML_FIELD_OP:

			status = acpi_ds_create_field (op, arg->common.node, walk_state);
			break;

		default:
			/* All NAMED_FIELD opcodes must be handled above */
			break;
		}
		break;


	 case AML_TYPE_NAMED_SIMPLE:

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		switch (op->common.aml_opcode) {
		case AML_PROCESSOR_OP:

			status = acpi_ex_create_processor (walk_state);
			break;

		case AML_POWER_RES_OP:

			status = acpi_ex_create_power_resource (walk_state);
			break;

		case AML_MUTEX_OP:

			status = acpi_ex_create_mutex (walk_state);
			break;

		case AML_EVENT_OP:

			status = acpi_ex_create_event (walk_state);
			break;

		case AML_DATA_REGION_OP:

			status = acpi_ex_create_table_region (walk_state);
			break;

		case AML_ALIAS_OP:

			status = acpi_ex_create_alias (walk_state);
			break;

		default:
			/* Unknown opcode */

			status = AE_OK;
			goto cleanup;
		}

		/* Delete operands */

		for (i = 1; i < walk_state->num_operands; i++) {
			acpi_ut_remove_reference (walk_state->operands[i]);
			walk_state->operands[i] = NULL;
		}

		break;
#endif /* ACPI_NO_METHOD_EXECUTION */

	case AML_TYPE_NAMED_COMPLEX:

		switch (op->common.aml_opcode) {
		case AML_METHOD_OP:
			/*
			 * Method_op Pkg_length Name_string Method_flags Term_list
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
				"LOADING-Method: State=%p Op=%p Named_obj=%p\n",
				walk_state, op, node));

			if (!acpi_ns_get_attached_object (node)) {
				status = acpi_ds_create_operands (walk_state, arg);
				if (ACPI_FAILURE (status)) {
					goto cleanup;
				}

				status = acpi_ex_create_method (op->named.data,
						   op->named.length, walk_state);
			}
			break;


#ifndef ACPI_NO_METHOD_EXECUTION
		case AML_REGION_OP:
			/*
			 * The Op_region is not fully parsed at this time. Only valid argument is the Space_id.
			 * (We must save the address of the AML of the address and length operands)
			 */
			/*
			 * If we have a valid region, initialize it
			 * Namespace is NOT locked at this point.
			 */
			status = acpi_ev_initialize_region (acpi_ns_get_attached_object (node), FALSE);
			if (ACPI_FAILURE (status)) {
				/*
				 *  If AE_NOT_EXIST is returned, it is not fatal
				 *  because many regions get created before a handler
				 *  is installed for said region.
				 */
				if (AE_NOT_EXIST == status) {
					status = AE_OK;
				}
			}
			break;


		case AML_NAME_OP:

			status = acpi_ds_create_node (walk_state, node, op);
			break;
#endif /* ACPI_NO_METHOD_EXECUTION */


		default:
			/* All NAMED_COMPLEX opcodes must be handled above */
			break;
		}
		break;


	case AML_CLASS_INTERNAL:

		/* case AML_INT_NAMEPATH_OP: */
		break;


	case AML_CLASS_METHOD_CALL:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"RESOLVING-Method_call: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		/*
		 * Lookup the method name and save the Node
		 */
		status = acpi_ns_lookup (walk_state->scope_info, arg->common.value.string,
				  ACPI_TYPE_ANY, ACPI_IMODE_LOAD_PASS2,
				  ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE,
				  walk_state, &(new_node));
		if (ACPI_SUCCESS (status)) {
			/*
			 * Make sure that what we found is indeed a method
			 * We didn't search for a method on purpose, to see if the name would resolve
			 */
			if (new_node->type != ACPI_TYPE_METHOD) {
				status = AE_AML_OPERAND_TYPE;
			}

			/* We could put the returned object (Node) on the object stack for later, but
			 * for now, we will put it in the "op" object that the parser uses, so we
			 * can get it again at the end of this scope
			 */
			op->common.node = new_node;
		}

		break;


	default:
		break;
	}


cleanup:

	/* Remove the Node pushed at the very beginning */

	walk_state->operands[0] = NULL;
	walk_state->num_operands = 0;
	return_ACPI_STATUS (status);
}


