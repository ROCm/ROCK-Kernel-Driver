/******************************************************************************
 *
 * Module Name: dswload - Dispatcher namespace load callbacks
 *              $Revision: 50 $
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


#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dswload")


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
		walk_state->parse_flags      |= ACPI_PARSE_EXECUTE  | ACPI_PARSE_DELETE_TREE;
		walk_state->descending_callback = acpi_ds_exec_begin_op;
		walk_state->ascending_callback = acpi_ds_exec_end_op;
		break;

	default:
		return (AE_BAD_PARAMETER);
		break;
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
	acpi_object_type8       data_type;
	NATIVE_CHAR             *path;


	PROC_NAME ("Ds_load1_begin_op");

	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	/* We are only interested in opcodes that have an associated name */

	if (walk_state->op) {
	   if (!(walk_state->op_info->flags & AML_NAMED)) {
			*out_op = op;
			return (AE_OK);
		}

		/* Check if this object has already been installed in the namespace */

		if (op->node) {
			*out_op = op;
			return (AE_OK);
		}
	}

	path = acpi_ps_get_next_namestring (&walk_state->parser_state);

	/* Map the raw opcode into an internal object type */

	data_type = acpi_ds_map_named_opcode_to_data_type (walk_state->opcode);


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"State=%p Op=%p Type=%x\n", walk_state, op, data_type));


	if (walk_state->opcode == AML_SCOPE_OP) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"State=%p Op=%p Type=%x\n", walk_state, op, data_type));
	}

	/*
	 * Enter the named type into the internal namespace.  We enter the name
	 * as we go downward in the parse tree.  Any necessary subobjects that involve
	 * arguments to the opcode must be created as we go back up the parse tree later.
	 */
	status = acpi_ns_lookup (walk_state->scope_info, path, data_type,
			  IMODE_LOAD_PASS1, NS_NO_UPSEARCH, walk_state, &(node));

	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (!op) {
		/* Create a new op */

		op = acpi_ps_alloc_op (walk_state->opcode);
		if (!op) {
			return (AE_NO_MEMORY);
		}
	}

	/* Initialize */

	((acpi_parse2_object *)op)->name = node->name;

	/*
	 * Put the Node in the "op" object that the parser uses, so we
	 * can get it again quickly when this scope is closed
	 */
	op->node = node;
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
	acpi_object_type8       data_type;


	PROC_NAME ("Ds_load1_end_op");

	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	/* We are only interested in opcodes that have an associated name */

	if (!(walk_state->op_info->flags & AML_NAMED)) {
		return (AE_OK);
	}

	/* Get the type to determine if we should pop the scope */

	data_type = acpi_ds_map_named_opcode_to_data_type (op->opcode);

	if (op->opcode == AML_NAME_OP) {
		/* For Name opcode, check the argument */

		if (op->value.arg) {
			data_type = acpi_ds_map_opcode_to_data_type (
					  (op->value.arg)->opcode, NULL);
			((acpi_namespace_node *)op->node)->type =
					  (u8) data_type;
		}
	}

	/* Pop the scope stack */

	if (acpi_ns_opens_scope (data_type)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "(%s): Popping scope for Op %p\n",
			acpi_ut_get_type_name (data_type), op));

		acpi_ds_scope_stack_pop (walk_state);
	}

	return (AE_OK);
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
	acpi_object_type8       data_type;
	NATIVE_CHAR             *buffer_ptr;
	void                    *original = NULL;


	PROC_NAME ("Ds_load2_begin_op");

	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	if (op) {
		/* We only care about Namespace opcodes here */

		if (!(walk_state->op_info->flags & AML_NSOPCODE) &&
			walk_state->opcode != AML_INT_NAMEPATH_OP) {
			return (AE_OK);
		}

		/* TBD: [Restructure] Temp! same code as in psparse */

		if (!(walk_state->op_info->flags & AML_NAMED)) {
			return (AE_OK);
		}

		/*
		 * Get the name we are going to enter or lookup in the namespace
		 */
		if (walk_state->opcode == AML_INT_NAMEPATH_OP) {
			/* For Namepath op, get the path string */

			buffer_ptr = op->value.string;
			if (!buffer_ptr) {
				/* No name, just exit */

				return (AE_OK);
			}
		}
		else {
			/* Get name from the op */

			buffer_ptr = (NATIVE_CHAR *) &((acpi_parse2_object *)op)->name;
		}
	}
	else {
		buffer_ptr = acpi_ps_get_next_namestring (&walk_state->parser_state);
	}


	/* Map the raw opcode into an internal object type */

	data_type = acpi_ds_map_named_opcode_to_data_type (walk_state->opcode);

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"State=%p Op=%p Type=%x\n", walk_state, op, data_type));


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
		status = acpi_ns_lookup (walk_state->scope_info, buffer_ptr, data_type,
				  IMODE_EXECUTE, NS_SEARCH_PARENT, walk_state, &(node));
	}

	else {
		if (op && op->node) {
			original = op->node;
			node = op->node;

			if (acpi_ns_opens_scope (data_type)) {
				status = acpi_ds_scope_stack_push (node, data_type, walk_state);
				if (ACPI_FAILURE (status)) {
					return (status);
				}

			}
			return (AE_OK);
		}

		/*
		 * Enter the named type into the internal namespace.  We enter the name
		 * as we go downward in the parse tree.  Any necessary subobjects that involve
		 * arguments to the opcode must be created as we go back up the parse tree later.
		 */
		status = acpi_ns_lookup (walk_state->scope_info, buffer_ptr, data_type,
				  IMODE_EXECUTE, NS_NO_UPSEARCH, walk_state, &(node));
	}

	if (ACPI_SUCCESS (status)) {
		if (!op) {
			/* Create a new op */

			op = acpi_ps_alloc_op (walk_state->opcode);
			if (!op) {
				return (AE_NO_MEMORY);
			}

			/* Initialize */

			((acpi_parse2_object *)op)->name = node->name;
			*out_op = op;
		}

		/*
		 * Put the Node in the "op" object that the parser uses, so we
		 * can get it again quickly when this scope is closed
		 */
		op->node = node;

		if (original) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "old %p new %p\n", original, node));

			if (original != node) {
				ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
					"Lookup match error: old %p new %p\n", original, node));
			}
		}
	}

	return (status);
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
	acpi_object_type8       data_type;
	acpi_namespace_node     *node;
	acpi_parse_object       *arg;
	acpi_namespace_node     *new_node;
	u32                     i;


	PROC_NAME ("Ds_load2_end_op");

	op = walk_state->op;
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	/* Only interested in opcodes that have namespace objects */

	if (!(walk_state->op_info->flags & AML_NSOBJECT)) {
		return (AE_OK);
	}

	if (op->opcode == AML_SCOPE_OP) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Ending scope Op=%p State=%p\n", op, walk_state));

		if (((acpi_parse2_object *)op)->name == -1) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unnamed scope! Op=%p State=%p\n",
				op, walk_state));
			return (AE_OK);
		}
	}


	data_type = acpi_ds_map_named_opcode_to_data_type (op->opcode);

	/*
	 * Get the Node/name from the earlier lookup
	 * (It was saved in the *op structure)
	 */
	node = op->node;

	/*
	 * Put the Node on the object stack (Contains the ACPI Name of
	 * this object)
	 */
	walk_state->operands[0] = (void *) node;
	walk_state->num_operands = 1;

	/* Pop the scope stack */

	if (acpi_ns_opens_scope (data_type)) {

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "(%s) Popping scope for Op %p\n",
			acpi_ut_get_type_name (data_type), op));
		acpi_ds_scope_stack_pop (walk_state);
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
		acpi_ps_get_opcode_name (op->opcode), walk_state, op, node));

	/* Decode the opcode */

	arg = op->value.arg;

	switch (walk_state->op_info->type) {
	case AML_TYPE_CREATE_FIELD:

		/*
		 * Create the field object, but the field buffer and index must
		 * be evaluated later during the execution phase
		 */
		status = acpi_ds_create_buffer_field (op, walk_state);
		break;


	 case AML_TYPE_NAMED_FIELD:

		arg = op->value.arg;
		switch (op->opcode) {
		case AML_INDEX_FIELD_OP:

			status = acpi_ds_create_index_field (op, (acpi_handle) arg->node,
					   walk_state);
			break;


		case AML_BANK_FIELD_OP:

			status = acpi_ds_create_bank_field (op, arg->node, walk_state);
			break;


		case AML_FIELD_OP:

			status = acpi_ds_create_field (op, arg->node, walk_state);
			break;
		}
		break;


	 case AML_TYPE_NAMED_SIMPLE:

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		switch (op->opcode) {
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
			break;
		}

		/* Delete operands */

		for (i = 1; i < walk_state->num_operands; i++) {
			acpi_ut_remove_reference (walk_state->operands[i]);
			walk_state->operands[i] = NULL;
		}

		break;


	case AML_TYPE_NAMED_COMPLEX:

		switch (op->opcode) {
		case AML_METHOD_OP:
			/*
			 * Method_op Pkg_length Names_string Method_flags Term_list
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
				"LOADING-Method: State=%p Op=%p Named_obj=%p\n",
				walk_state, op, node));

			if (!node->object) {
				status = acpi_ds_create_operands (walk_state, arg);
				if (ACPI_FAILURE (status)) {
					goto cleanup;
				}

				status = acpi_ex_create_method (((acpi_parse2_object *) op)->data,
						   ((acpi_parse2_object *) op)->length,
						   walk_state);
			}
			break;


		case AML_REGION_OP:
			/*
			 * The Op_region is not fully parsed at this time. Only valid argument is the Space_id.
			 * (We must save the address of the AML of the address and length operands)
			 */
			status = acpi_ex_create_region (((acpi_parse2_object *) op)->data,
					  ((acpi_parse2_object *) op)->length,
							 (ACPI_ADR_SPACE_TYPE) arg->value.integer, walk_state);
			break;


		case AML_NAME_OP:

			status = acpi_ds_create_node (walk_state, node, op);
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
		status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
				  ACPI_TYPE_ANY, IMODE_LOAD_PASS2,
				  NS_SEARCH_PARENT | NS_DONT_OPEN_SCOPE,
				  walk_state, &(new_node));
		if (ACPI_SUCCESS (status)) {
			/* TBD: has name already been resolved by here ??*/

			/* TBD: [Restructure] Make sure that what we found is indeed a method! */
			/* We didn't search for a method on purpose, to see if the name would resolve! */

			/* We could put the returned object (Node) on the object stack for later, but
			 * for now, we will put it in the "op" object that the parser uses, so we
			 * can get it again at the end of this scope
			 */
			op->node = new_node;
		}

		break;


	default:
		break;
	}


cleanup:

	/* Remove the Node pushed at the very beginning */

	walk_state->operands[0] = NULL;
	walk_state->num_operands = 0;
	return (status);
}


