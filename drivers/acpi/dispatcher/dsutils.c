/*******************************************************************************
 *
 * Module Name: dsutils - Dispatcher utilities
 *              $Revision: 80 $
 *
 ******************************************************************************/

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
#include "acdebug.h"

#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dsutils")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_is_result_used
 *
 * PARAMETERS:  Op
 *              Result_obj
 *              Walk_state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check if a result object will be used by the parent
 *
 ******************************************************************************/

u8
acpi_ds_is_result_used (
	acpi_parse_object       *op,
	acpi_walk_state         *walk_state)
{
	const acpi_opcode_info  *parent_info;


	FUNCTION_TRACE_PTR ("Ds_is_result_used", op);


	/* Must have both an Op and a Result Object */

	if (!op) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Op\n"));
		return_VALUE (TRUE);
	}


	/*
	 * If there is no parent, the result can't possibly be used!
	 * (An executing method typically has no parent, since each
	 * method is parsed separately)  However, a method that is
	 * invoked from another method has a parent.
	 */
	if (!op->parent) {
		return_VALUE (FALSE);
	}


	/*
	 * Get info on the parent.  The root Op is AML_SCOPE
	 */

	parent_info = acpi_ps_get_opcode_info (op->parent->opcode);
	if (parent_info->class == AML_CLASS_UNKNOWN) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown parent opcode. Op=%p\n", op));
		return_VALUE (FALSE);
	}


	/*
	 * Decide what to do with the result based on the parent.  If
	 * the parent opcode will not use the result, delete the object.
	 * Otherwise leave it as is, it will be deleted when it is used
	 * as an operand later.
	 */
	switch (parent_info->class) {
	/*
	 * In these cases, the parent will never use the return object
	 */
	case AML_CLASS_CONTROL:        /* IF, ELSE, WHILE only */

		switch (op->parent->opcode) {
		case AML_RETURN_OP:

			/* Never delete the return value associated with a return opcode */

			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
				"Result used, [RETURN] opcode=%X Op=%p\n", op->opcode, op));
			return_VALUE (TRUE);
			break;

		case AML_IF_OP:
		case AML_WHILE_OP:

			/*
			 * If we are executing the predicate AND this is the predicate op,
			 * we will use the return value!
			 */
			if ((walk_state->control_state->common.state == CONTROL_PREDICATE_EXECUTING) &&
				(walk_state->control_state->control.predicate_op == op)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
					"Result used as a predicate, [IF/WHILE] opcode=%X Op=%p\n",
					op->opcode, op));
				return_VALUE (TRUE);
			}

			break;
		}


		/* Fall through to not used case below */


	case AML_CLASS_NAMED_OBJECT:   /* Scope, method, etc. */
	case AML_CLASS_CREATE:

		/*
		 * These opcodes allow Term_arg(s) as operands and therefore
		 * method calls.  The result is used.
		 */
		if ((op->parent->opcode == AML_REGION_OP)               ||
			(op->parent->opcode == AML_CREATE_FIELD_OP)         ||
			(op->parent->opcode == AML_CREATE_BIT_FIELD_OP)     ||
			(op->parent->opcode == AML_CREATE_BYTE_FIELD_OP)    ||
			(op->parent->opcode == AML_CREATE_WORD_FIELD_OP)    ||
			(op->parent->opcode == AML_CREATE_DWORD_FIELD_OP)   ||
			(op->parent->opcode == AML_CREATE_QWORD_FIELD_OP)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
				"Result used, [Region or Create_field] opcode=%X Op=%p\n",
				op->opcode, op));
			return_VALUE (TRUE);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
			"Result not used, Parent opcode=%X Op=%p\n", op->opcode, op));

		return_VALUE (FALSE);
		break;

	/*
	 * In all other cases. the parent will actually use the return
	 * object, so keep it.
	 */
	default:
		break;
	}

	return_VALUE (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_delete_result_if_not_used
 *
 * PARAMETERS:  Op
 *              Result_obj
 *              Walk_state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Used after interpretation of an opcode.  If there is an internal
 *              result descriptor, check if the parent opcode will actually use
 *              this result.  If not, delete the result now so that it will
 *              not become orphaned.
 *
 ******************************************************************************/

void
acpi_ds_delete_result_if_not_used (
	acpi_parse_object       *op,
	acpi_operand_object     *result_obj,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	FUNCTION_TRACE_PTR ("Ds_delete_result_if_not_used", result_obj);


	if (!op) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Op\n"));
		return_VOID;
	}

	if (!result_obj) {
		return_VOID;
	}


	if (!acpi_ds_is_result_used (op, walk_state)) {
		/*
		 * Must pop the result stack (Obj_desc should be equal to Result_obj)
		 */
		status = acpi_ds_result_pop (&obj_desc, walk_state);
		if (ACPI_SUCCESS (status)) {
			acpi_ut_remove_reference (result_obj);
		}
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_operand
 *
 * PARAMETERS:  Walk_state
 *              Arg
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parse tree object that is an argument to an AML
 *              opcode to the equivalent interpreter object.  This may include
 *              looking up a name or entering a new name into the internal
 *              namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_operand (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *arg,
	u32                     arg_index)
{
	acpi_status             status = AE_OK;
	NATIVE_CHAR             *name_string;
	u32                     name_length;
	acpi_object_type8       data_type;
	acpi_operand_object     *obj_desc;
	acpi_parse_object       *parent_op;
	u16                     opcode;
	u32                     flags;
	operating_mode          interpreter_mode;
	const acpi_opcode_info  *op_info;


	FUNCTION_TRACE_PTR ("Ds_create_operand", arg);


	/* A valid name must be looked up in the namespace */

	if ((arg->opcode == AML_INT_NAMEPATH_OP) &&
		(arg->value.string)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Getting a name: Arg=%p\n", arg));

		/* Get the entire name string from the AML stream */

		status = acpi_ex_get_name_string (ACPI_TYPE_ANY, arg->value.buffer,
				  &name_string, &name_length);

		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * All prefixes have been handled, and the name is
		 * in Name_string
		 */

		/*
		 * Differentiate between a namespace "create" operation
		 * versus a "lookup" operation (IMODE_LOAD_PASS2 vs.
		 * IMODE_EXECUTE) in order to support the creation of
		 * namespace objects during the execution of control methods.
		 */
		parent_op = arg->parent;
		op_info = acpi_ps_get_opcode_info (parent_op->opcode);
		if ((op_info->flags & AML_NSNODE) &&
			(parent_op->opcode != AML_INT_METHODCALL_OP) &&
			(parent_op->opcode != AML_REGION_OP) &&
			(parent_op->opcode != AML_INT_NAMEPATH_OP)) {
			/* Enter name into namespace if not found */

			interpreter_mode = IMODE_LOAD_PASS2;
		}

		else {
			/* Return a failure if name not found */

			interpreter_mode = IMODE_EXECUTE;
		}

		status = acpi_ns_lookup (walk_state->scope_info, name_string,
				 ACPI_TYPE_ANY, interpreter_mode,
				 NS_SEARCH_PARENT | NS_DONT_OPEN_SCOPE,
				 walk_state,
				 (acpi_namespace_node **) &obj_desc);

		/* Free the namestring created above */

		ACPI_MEM_FREE (name_string);

		/*
		 * The only case where we pass through (ignore) a NOT_FOUND
		 * error is for the Cond_ref_of opcode.
		 */
		if (status == AE_NOT_FOUND) {
			if (parent_op->opcode == AML_COND_REF_OF_OP) {
				/*
				 * For the Conditional Reference op, it's OK if
				 * the name is not found;  We just need a way to
				 * indicate this to the interpreter, set the
				 * object to the root
				 */
				obj_desc = (acpi_operand_object *) acpi_gbl_root_node;
				status = AE_OK;
			}

			else {
				/*
				 * We just plain didn't find it -- which is a
				 * very serious error at this point
				 */
				status = AE_AML_NAME_NOT_FOUND;

				/* TBD: Externalize Name_string and print */

				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"Object name was not found in namespace\n"));
			}
		}

		/* Check status from the lookup */

		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Put the resulting object onto the current object stack */

		status = acpi_ds_obj_stack_push (obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
		DEBUGGER_EXEC (acpi_db_display_argument_object (obj_desc, walk_state));
	}


	else {
		/* Check for null name case */

		if (arg->opcode == AML_INT_NAMEPATH_OP) {
			/*
			 * If the name is null, this means that this is an
			 * optional result parameter that was not specified
			 * in the original ASL.  Create an Reference for a
			 * placeholder
			 */
			opcode = AML_ZERO_OP;       /* Has no arguments! */

			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Null namepath: Arg=%p\n", arg));

			/*
			 * TBD: [Investigate] anything else needed for the
			 * zero op lvalue?
			 */
		}

		else {
			opcode = arg->opcode;
		}


		/* Get the data type of the argument */

		data_type = acpi_ds_map_opcode_to_data_type (opcode, &flags);
		if (data_type == INTERNAL_TYPE_INVALID) {
			return_ACPI_STATUS (AE_NOT_IMPLEMENTED);
		}

		if (flags & OP_HAS_RETURN_VALUE) {
			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
				"Argument previously created, already stacked \n"));

			DEBUGGER_EXEC (acpi_db_display_argument_object (walk_state->operands [walk_state->num_operands - 1], walk_state));

			/*
			 * Use value that was already previously returned
			 * by the evaluation of this argument
			 */
			status = acpi_ds_result_pop_from_bottom (&obj_desc, walk_state);
			if (ACPI_FAILURE (status)) {
				/*
				 * Only error is underflow, and this indicates
				 * a missing or null operand!
				 */
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Missing or null operand, %s\n",
					acpi_format_exception (status)));
				return_ACPI_STATUS (status);
			}

		}

		else {
			/* Create an ACPI_INTERNAL_OBJECT for the argument */

			obj_desc = acpi_ut_create_internal_object (data_type);
			if (!obj_desc) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			/* Initialize the new object */

			status = acpi_ds_init_object_from_op (walk_state, arg,
					 opcode, &obj_desc);
			if (ACPI_FAILURE (status)) {
				acpi_ut_delete_object_desc (obj_desc);
				return_ACPI_STATUS (status);
			}
	   }

		/* Put the operand object on the object stack */

		status = acpi_ds_obj_stack_push (obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		DEBUGGER_EXEC (acpi_db_display_argument_object (obj_desc, walk_state));
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_operands
 *
 * PARAMETERS:  First_arg           - First argument of a parser argument tree
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an operator's arguments from a parse tree format to
 *              namespace objects and place those argument object on the object
 *              stack in preparation for evaluation by the interpreter.
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_operands (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *first_arg)
{
	acpi_status             status = AE_OK;
	acpi_parse_object       *arg;
	u32                     arg_count = 0;


	FUNCTION_TRACE_PTR ("Ds_create_operands", first_arg);


	/* For all arguments in the list... */

	arg = first_arg;
	while (arg) {
		status = acpi_ds_create_operand (walk_state, arg, arg_count);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Arg #%d (%p) done, Arg1=%p\n",
			arg_count, arg, first_arg));

		/* Move on to next argument, if any */

		arg = arg->next;
		arg_count++;
	}

	return_ACPI_STATUS (status);


cleanup:
	/*
	 * We must undo everything done above; meaning that we must
	 * pop everything off of the operand stack and delete those
	 * objects
	 */
	acpi_ds_obj_stack_pop_and_delete (arg_count, walk_state);

	ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "While creating Arg %d - %s\n",
		(arg_count + 1), acpi_format_exception (status)));
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_resolve_operands
 *
 * PARAMETERS:  Walk_state          - Current walk state with operands on stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve all operands to their values.  Used to prepare
 *              arguments to a control method invocation (a call from one
 *              method to another.)
 *
 ******************************************************************************/

acpi_status
acpi_ds_resolve_operands (
	acpi_walk_state         *walk_state)
{
	u32                     i;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_PTR ("Ds_resolve_operands", walk_state);


	/*
	 * Attempt to resolve each of the valid operands
	 * Method arguments are passed by value, not by reference
	 */

	/*
	 * TBD: [Investigate] Note from previous parser:
	 *   Ref_of problem with Acpi_ex_resolve_to_value() conversion.
	 */
	for (i = 0; i < walk_state->num_operands; i++) {
		status = acpi_ex_resolve_to_value (&walk_state->operands[i], walk_state);
		if (ACPI_FAILURE (status)) {
			break;
		}
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_map_opcode_to_data_type
 *
 * PARAMETERS:  Opcode          - AML opcode to map
 *              Out_flags       - Additional info about the opcode
 *
 * RETURN:      The ACPI type associated with the opcode
 *
 * DESCRIPTION: Convert a raw AML opcode to the associated ACPI data type,
 *              if any.  If the opcode returns a value as part of the
 *              intepreter execution, a flag is returned in Out_flags.
 *
 ******************************************************************************/

acpi_object_type8
acpi_ds_map_opcode_to_data_type (
	u16                     opcode,
	u32                     *out_flags)
{
	acpi_object_type8       data_type = INTERNAL_TYPE_INVALID;
	const acpi_opcode_info  *op_info;
	u32                     flags = 0;


	PROC_NAME ("Ds_map_opcode_to_data_type");


	op_info = acpi_ps_get_opcode_info (opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		/* Unknown opcode */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown AML opcode: %x\n", opcode));
		return (data_type);
	}


/*
 * TBD: Use op class
 */

	switch (op_info->type) {

	case AML_TYPE_LITERAL:

		switch (opcode) {
		case AML_BYTE_OP:
		case AML_WORD_OP:
		case AML_DWORD_OP:
		case AML_QWORD_OP:

			data_type = ACPI_TYPE_INTEGER;
			break;


		case AML_STRING_OP:

			data_type = ACPI_TYPE_STRING;
			break;

		case AML_INT_NAMEPATH_OP:
			data_type = INTERNAL_TYPE_REFERENCE;
			break;

		default:
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Unknown (type LITERAL) AML opcode: %x\n", opcode));
			break;
		}
		break;


	case AML_TYPE_DATA_TERM:

		switch (opcode) {
		case AML_BUFFER_OP:

			data_type = ACPI_TYPE_BUFFER;
			break;

		case AML_PACKAGE_OP:
		case AML_VAR_PACKAGE_OP:

			data_type = ACPI_TYPE_PACKAGE;
			break;

		default:
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Unknown (type DATA_TERM) AML opcode: %x\n", opcode));
			break;
		}
		break;


	case AML_TYPE_CONSTANT:
	case AML_TYPE_METHOD_ARGUMENT:
	case AML_TYPE_LOCAL_VARIABLE:

		data_type = INTERNAL_TYPE_REFERENCE;
		break;


	case AML_TYPE_EXEC_1A_0T_1R:
	case AML_TYPE_EXEC_1A_1T_1R:
	case AML_TYPE_EXEC_2A_0T_1R:
	case AML_TYPE_EXEC_2A_1T_1R:
	case AML_TYPE_EXEC_2A_2T_1R:
	case AML_TYPE_EXEC_3A_1T_1R:
	case AML_TYPE_EXEC_6A_0T_1R:
	case AML_TYPE_RETURN:

		flags = OP_HAS_RETURN_VALUE;
		data_type = ACPI_TYPE_ANY;
		break;


	case AML_TYPE_METHOD_CALL:

		flags = OP_HAS_RETURN_VALUE;
		data_type = ACPI_TYPE_METHOD;
		break;


	case AML_TYPE_NAMED_FIELD:
	case AML_TYPE_NAMED_SIMPLE:
	case AML_TYPE_NAMED_COMPLEX:
	case AML_TYPE_NAMED_NO_OBJ:

		data_type = acpi_ds_map_named_opcode_to_data_type (opcode);
		break;


	case AML_TYPE_EXEC_1A_0T_0R:
	case AML_TYPE_EXEC_2A_0T_0R:
	case AML_TYPE_EXEC_3A_0T_0R:
	case AML_TYPE_EXEC_1A_1T_0R:
	case AML_TYPE_CONTROL:

		/* No mapping needed at this time */

		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Unimplemented data type opcode: %x\n", opcode));
		break;
	}

	/* Return flags to caller if requested */

	if (out_flags) {
		*out_flags = flags;
	}

	return (data_type);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_map_named_opcode_to_data_type
 *
 * PARAMETERS:  Opcode              - The Named AML opcode to map
 *
 * RETURN:      The ACPI type associated with the named opcode
 *
 * DESCRIPTION: Convert a raw Named AML opcode to the associated data type.
 *              Named opcodes are a subsystem of the AML opcodes.
 *
 ******************************************************************************/

acpi_object_type8
acpi_ds_map_named_opcode_to_data_type (
	u16                     opcode)
{
	acpi_object_type8       data_type;


	FUNCTION_ENTRY ();


	/* Decode Opcode */

	switch (opcode) {
	case AML_SCOPE_OP:
		data_type = INTERNAL_TYPE_SCOPE;
		break;

	case AML_DEVICE_OP:
		data_type = ACPI_TYPE_DEVICE;
		break;

	case AML_THERMAL_ZONE_OP:
		data_type = ACPI_TYPE_THERMAL;
		break;

	case AML_METHOD_OP:
		data_type = ACPI_TYPE_METHOD;
		break;

	case AML_POWER_RES_OP:
		data_type = ACPI_TYPE_POWER;
		break;

	case AML_PROCESSOR_OP:
		data_type = ACPI_TYPE_PROCESSOR;
		break;

	case AML_FIELD_OP:                              /* Field_op */
		data_type = INTERNAL_TYPE_FIELD_DEFN;
		break;

	case AML_INDEX_FIELD_OP:                        /* Index_field_op */
		data_type = INTERNAL_TYPE_INDEX_FIELD_DEFN;
		break;

	case AML_BANK_FIELD_OP:                         /* Bank_field_op */
		data_type = INTERNAL_TYPE_BANK_FIELD_DEFN;
		break;

	case AML_INT_NAMEDFIELD_OP:                     /* NO CASE IN ORIGINAL  */
		data_type = ACPI_TYPE_ANY;
		break;

	case AML_NAME_OP:                               /* Name_op - special code in original */
	case AML_INT_NAMEPATH_OP:
		data_type = ACPI_TYPE_ANY;
		break;

	case AML_ALIAS_OP:
		data_type = INTERNAL_TYPE_ALIAS;
		break;

	case AML_MUTEX_OP:
		data_type = ACPI_TYPE_MUTEX;
		break;

	case AML_EVENT_OP:
		data_type = ACPI_TYPE_EVENT;
		break;

	case AML_DATA_REGION_OP:
	case AML_REGION_OP:
		data_type = ACPI_TYPE_REGION;
		break;


	default:
		data_type = ACPI_TYPE_ANY;
		break;

	}

	return (data_type);
}


