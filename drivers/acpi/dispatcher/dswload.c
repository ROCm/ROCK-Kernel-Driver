/******************************************************************************
 *
 * Module Name: dswload - Dispatcher namespace load callbacks
 *              $Revision: 44 $
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
	u16                     opcode,
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op)
{
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_object_type8       data_type;
	NATIVE_CHAR             *path;
	const acpi_opcode_info  *op_info;


	PROC_NAME ("Ds_load1_begin_op");
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	/* We are only interested in opcodes that have an associated name */

	op_info = acpi_ps_get_opcode_info (opcode);
	if (!(op_info->flags & AML_NAMED)) {
		*out_op = op;
		return (AE_OK);
	}

	/* Check if this object has already been installed in the namespace */

	if (op && op->node) {
		*out_op = op;
		return (AE_OK);
	}

	path = acpi_ps_get_next_namestring (walk_state->parser_state);

	/* Map the raw opcode into an internal object type */

	data_type = acpi_ds_map_named_opcode_to_data_type (opcode);


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"State=%p Op=%p Type=%x\n", walk_state, op, data_type));


	if (opcode == AML_SCOPE_OP) {
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

		op = acpi_ps_alloc_op (opcode);
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
	acpi_ps_append_arg (acpi_ps_get_parent_scope (walk_state->parser_state), op);

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
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_object_type8       data_type;
	const acpi_opcode_info  *op_info;


	PROC_NAME ("Ds_load1_end_op");
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	/* We are only interested in opcodes that have an associated name */

	op_info = acpi_ps_get_opcode_info (op->opcode);
	if (!(op_info->flags & AML_NAMED)) {
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
	u16                     opcode,
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op)
{
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_object_type8       data_type;
	NATIVE_CHAR             *buffer_ptr;
	void                    *original = NULL;
	const acpi_opcode_info  *op_info;


	PROC_NAME ("Ds_load2_begin_op");
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	/* We only care about Namespace opcodes here */

	op_info = acpi_ps_get_opcode_info (opcode);
	if (!(op_info->flags & AML_NSOPCODE) &&
		opcode != AML_INT_NAMEPATH_OP) {
		return (AE_OK);
	}

	/* TBD: [Restructure] Temp! same code as in psparse */

	if (!(op_info->flags & AML_NAMED)) {
		return (AE_OK);
	}

	if (op) {
		/*
		 * Get the name we are going to enter or lookup in the namespace
		 */
		if (opcode == AML_INT_NAMEPATH_OP) {
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
		buffer_ptr = acpi_ps_get_next_namestring (walk_state->parser_state);
	}


	/* Map the raw opcode into an internal object type */

	data_type = acpi_ds_map_named_opcode_to_data_type (opcode);

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"State=%p Op=%p Type=%x\n", walk_state, op, data_type));


	if (opcode == AML_FIELD_OP          ||
		opcode == AML_BANK_FIELD_OP     ||
		opcode == AML_INDEX_FIELD_OP) {
		node = NULL;
		status = AE_OK;
	}

	else if (opcode == AML_INT_NAMEPATH_OP) {
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

			op = acpi_ps_alloc_op (opcode);
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
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_status             status = AE_OK;
	acpi_object_type8       data_type;
	acpi_namespace_node     *node;
	acpi_parse_object       *arg;
	acpi_namespace_node     *new_node;
	const acpi_opcode_info  *op_info;


	PROC_NAME ("Ds_load2_end_op");
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op, walk_state));


	/* Only interested in opcodes that have namespace objects */

	op_info = acpi_ps_get_opcode_info (op->opcode);
	if (!(op_info->flags & AML_NSOBJECT)) {
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
	 * AML_SCOPE
	 * AML_DEVICE
	 * AML_THERMALZONE
	 * AML_METHOD
	 * AML_POWERRES
	 * AML_PROCESSOR
	 * AML_FIELD
	 * AML_INDEXFIELD
	 * AML_BANKFIELD
	 * AML_NAMEDFIELD
	 * AML_NAME
	 * AML_ALIAS
	 * AML_MUTEX
	 * AML_EVENT
	 * AML_OPREGION
	 * AML_CREATEFIELD
	 * AML_CREATEBITFIELD
	 * AML_CREATEBYTEFIELD
	 * AML_CREATEWORDFIELD
	 * AML_CREATEDWORDFIELD
	 * AML_CREATEQWORDFIELD
	 * AML_METHODCALL
	 */


	/* Decode the opcode */

	arg = op->value.arg;

	switch (op->opcode) {

	case AML_CREATE_FIELD_OP:
	case AML_CREATE_BIT_FIELD_OP:
	case AML_CREATE_BYTE_FIELD_OP:
	case AML_CREATE_WORD_FIELD_OP:
	case AML_CREATE_DWORD_FIELD_OP:
	case AML_CREATE_QWORD_FIELD_OP:

		/*
		 * Create the field object, but the field buffer and index must
		 * be evaluated later during the execution phase
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Create_xxx_field: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		/* Get the Name_string argument */

		if (op->opcode == AML_CREATE_FIELD_OP) {
			arg = acpi_ps_get_arg (op, 3);
		}
		else {
			/* Create Bit/Byte/Word/Dword field */

			arg = acpi_ps_get_arg (op, 2);
		}

		if (!arg) {
			status = AE_AML_NO_OPERAND;
			goto cleanup;
		}

		/*
		 * Enter the Name_string into the namespace
		 */
		status = acpi_ns_lookup (walk_state->scope_info, arg->value.string,
				 INTERNAL_TYPE_DEF_ANY, IMODE_LOAD_PASS1,
				 NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE,
				 walk_state, &(new_node));
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/* We could put the returned object (Node) on the object stack for later, but
		 * for now, we will put it in the "op" object that the parser uses, so we
		 * can get it again at the end of this scope
		 */
		op->node = new_node;

		/*
		 * If there is no object attached to the node, this node was just created and
		 * we need to create the field object.  Otherwise, this was a lookup of an
		 * existing node and we don't want to create the field object again.
		 */
		if (!new_node->object) {
			/*
			 * The Field definition is not fully parsed at this time.
			 * (We must save the address of the AML for the buffer and index operands)
			 */
			status = acpi_ex_create_buffer_field (((acpi_parse2_object *) op)->data,
					  ((acpi_parse2_object *) op)->length,
					  new_node, walk_state);
		}
		break;


	case AML_INT_METHODCALL_OP:

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


	case AML_PROCESSOR_OP:

		/* Nothing to do other than enter object into namespace */

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Processor: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		status = acpi_ex_create_processor (op, node);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Completed Processor Init, Op=%p State=%p entry=%p\n",
			op, walk_state, node));
		break;


	case AML_POWER_RES_OP:

		/* Nothing to do other than enter object into namespace */

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Power_resource: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		status = acpi_ex_create_power_resource (op, node);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Completed Power_resource Init, Op=%p State=%p entry=%p\n",
			op, walk_state, node));
		break;


	case AML_THERMAL_ZONE_OP:

		/* Nothing to do other than enter object into namespace */

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Thermal_zone: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));
		break;


	case AML_FIELD_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Field: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		arg = op->value.arg;

		status = acpi_ds_create_field (op, arg->node, walk_state);
		break;


	case AML_INDEX_FIELD_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Index_field: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		arg = op->value.arg;

		status = acpi_ds_create_index_field (op, (acpi_handle) arg->node,
				   walk_state);
		break;


	case AML_BANK_FIELD_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Bank_field: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		arg = op->value.arg;
		status = acpi_ds_create_bank_field (op, arg->node, walk_state);
		break;


	/*
	 * Method_op Pkg_length Names_string Method_flags Term_list
	 */
	case AML_METHOD_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Method: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));

		if (!node->object) {
			status = acpi_ex_create_method (((acpi_parse2_object *) op)->data,
					   ((acpi_parse2_object *) op)->length,
					   arg->value.integer32, node);
		}
		break;


	case AML_MUTEX_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Mutex: Op=%p State=%p\n", op, walk_state));

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		status = acpi_ex_create_mutex (walk_state);
		break;


	case AML_EVENT_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Event: Op=%p State=%p\n", op, walk_state));

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		status = acpi_ex_create_event (walk_state);
		break;


	case AML_REGION_OP:

		if (node->object) {
			break;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Opregion: Op=%p State=%p Named_obj=%p\n",
			op, walk_state, node));

		/*
		 * The Op_region is not fully parsed at this time. Only valid argument is the Space_id.
		 * (We must save the address of the AML of the address and length operands)
		 */
		status = acpi_ex_create_region (((acpi_parse2_object *) op)->data,
				  ((acpi_parse2_object *) op)->length,
						 (ACPI_ADR_SPACE_TYPE) arg->value.integer, walk_state);

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Completed Op_region Init, Op=%p State=%p entry=%p\n",
			op, walk_state, node));
		break;


	/* Namespace Modifier Opcodes */

	case AML_ALIAS_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Alias: Op=%p State=%p\n", op, walk_state));

		status = acpi_ds_create_operands (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		status = acpi_ex_create_alias (walk_state);
		break;


	case AML_NAME_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Name: Op=%p State=%p\n", op, walk_state));

		/*
		 * Because of the execution pass through the non-control-method
		 * parts of the table, we can arrive here twice.  Only init
		 * the named object node the first time through
		 */
		if (!node->object) {
			status = acpi_ds_create_node (walk_state, node, op);
		}

		break;


	case AML_INT_NAMEPATH_OP:

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"LOADING-Name_path object: State=%p Op=%p Named_obj=%p\n",
			walk_state, op, node));
		break;


	default:
		break;
	}


cleanup:

	/* Remove the Node pushed at the very beginning */

	acpi_ds_obj_stack_pop (1, walk_state);
	return (status);
}


