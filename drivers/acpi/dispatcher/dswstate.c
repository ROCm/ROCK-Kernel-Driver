/******************************************************************************
 *
 * Module Name: dswstate - Dispatcher parse tree walk management routines
 *              $Revision: 54 $
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
#include "amlcode.h"
#include "acparser.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dswstate")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_result_insert
 *
 * PARAMETERS:  Object              - Object to push
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto this walk's result stack
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_insert (
	void                    *object,
	u32                     index,
	acpi_walk_state         *walk_state)
{
	acpi_generic_state      *state;


	PROC_NAME ("Ds_result_insert");


	state = walk_state->results;
	if (!state) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No result object pushed! State=%p\n",
			walk_state));
		return (AE_NOT_EXIST);
	}

	if (index >= OBJ_NUM_OPERANDS) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Index out of range: %X Obj=%p State=%p Num=%X\n",
			index, object, walk_state, state->results.num_results));
		return (AE_BAD_PARAMETER);
	}

	if (!object) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Null Object! Index=%X Obj=%p State=%p Num=%X\n",
			index, object, walk_state, state->results.num_results));
		return (AE_BAD_PARAMETER);
	}

	state->results.obj_desc [index] = object;
	state->results.num_results++;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"Obj=%p [%s] State=%p Num=%X Cur=%X\n",
		object, object ? acpi_ut_get_type_name (((acpi_operand_object *) object)->common.type) : "NULL",
		walk_state, state->results.num_results, walk_state->current_result));

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_result_remove
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off the bottom of this walk's result stack.  In
 *              other words, this is a FIFO.
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_remove (
	acpi_operand_object     **object,
	u32                     index,
	acpi_walk_state         *walk_state)
{
	acpi_generic_state      *state;


	PROC_NAME ("Ds_result_remove");


	state = walk_state->results;
	if (!state) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No result object pushed! State=%p\n",
			walk_state));
		return (AE_NOT_EXIST);
	}

	if (index >= OBJ_NUM_OPERANDS) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Index out of range: %X State=%p Num=%X\n",
			index, walk_state, state->results.num_results));
	}


	/* Check for a valid result object */

	if (!state->results.obj_desc [index]) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Null operand! State=%p #Ops=%X, Index=%X\n",
			walk_state, state->results.num_results, index));
		return (AE_AML_NO_RETURN_VALUE);
	}

	/* Remove the object */

	state->results.num_results--;

	*object = state->results.obj_desc [index];
	state->results.obj_desc [index] = NULL;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"Obj=%p [%s] Index=%X State=%p Num=%X\n",
		*object, (*object) ? acpi_ut_get_type_name ((*object)->common.type) : "NULL",
		index, walk_state, state->results.num_results));

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_result_pop
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off the bottom of this walk's result stack.  In
 *              other words, this is a FIFO.
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_pop (
	acpi_operand_object     **object,
	acpi_walk_state         *walk_state)
{
	u32                     index;
	acpi_generic_state      *state;


	PROC_NAME ("Ds_result_pop");


	state = walk_state->results;
	if (!state) {
		return (AE_OK);
	}

	if (!state->results.num_results) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Result stack is empty! State=%p\n",
			walk_state));
		return (AE_AML_NO_RETURN_VALUE);
	}

	/* Remove top element */

	state->results.num_results--;

	for (index = OBJ_NUM_OPERANDS; index; index--) {
		/* Check for a valid result object */

		if (state->results.obj_desc [index -1]) {
			*object = state->results.obj_desc [index -1];
			state->results.obj_desc [index -1] = NULL;

			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p [%s] Index=%X State=%p Num=%X\n",
				*object, (*object) ? acpi_ut_get_type_name ((*object)->common.type) : "NULL",
				index -1, walk_state, state->results.num_results));

			return (AE_OK);
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No result objects! State=%p\n", walk_state));
	return (AE_AML_NO_RETURN_VALUE);
}

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_result_pop_from_bottom
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off the bottom of this walk's result stack.  In
 *              other words, this is a FIFO.
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_pop_from_bottom (
	acpi_operand_object     **object,
	acpi_walk_state         *walk_state)
{
	u32                     index;
	acpi_generic_state      *state;


	PROC_NAME ("Ds_result_pop_from_bottom");


	state = walk_state->results;
	if (!state) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Warning: No result object pushed! State=%p\n", walk_state));
		return (AE_NOT_EXIST);
	}


	if (!state->results.num_results) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No result objects! State=%p\n", walk_state));
		return (AE_AML_NO_RETURN_VALUE);
	}

	/* Remove Bottom element */

	*object = state->results.obj_desc [0];

	/* Push entire stack down one element */

	for (index = 0; index < state->results.num_results; index++) {
		state->results.obj_desc [index] = state->results.obj_desc [index + 1];
	}

	state->results.num_results--;

	/* Check for a valid result object */

	if (!*object) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null operand! State=%p #Ops=%X, Index=%X\n",
			walk_state, state->results.num_results, index));
		return (AE_AML_NO_RETURN_VALUE);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p [%s], Results=%p State=%p\n",
		*object, (*object) ? acpi_ut_get_type_name ((*object)->common.type) : "NULL",
		state, walk_state));


	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_result_push
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto the current result stack
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_push (
	acpi_operand_object     *object,
	acpi_walk_state         *walk_state)
{
	acpi_generic_state      *state;


	PROC_NAME ("Ds_result_push");


	state = walk_state->results;
	if (!state) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No result stack frame\n"));
		return (AE_AML_INTERNAL);
	}

	if (state->results.num_results == OBJ_NUM_OPERANDS) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Result stack overflow: Obj=%p State=%p Num=%X\n",
			object, walk_state, state->results.num_results));
		return (AE_STACK_OVERFLOW);
	}

	if (!object) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null Object! Obj=%p State=%p Num=%X\n",
			object, walk_state, state->results.num_results));
		return (AE_BAD_PARAMETER);
	}


	state->results.obj_desc [state->results.num_results] = object;
	state->results.num_results++;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p [%s] State=%p Num=%X Cur=%X\n",
		object, object ? acpi_ut_get_type_name (((acpi_operand_object *) object)->common.type) : "NULL",
		walk_state, state->results.num_results, walk_state->current_result));

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_result_stack_push
 *
 * PARAMETERS:  Object              - Object to push
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_stack_push (
	acpi_walk_state         *walk_state)
{
	acpi_generic_state      *state;

	PROC_NAME ("Ds_result_stack_push");


	state = acpi_ut_create_generic_state ();
	if (!state) {
		return (AE_NO_MEMORY);
	}

	state->common.data_type = ACPI_DESC_TYPE_STATE_RESULT;
	acpi_ut_push_generic_state (&walk_state->results, state);

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Results=%p State=%p\n",
		state, walk_state));

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_result_stack_pop
 *
 * PARAMETERS:  Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_stack_pop (
	acpi_walk_state         *walk_state)
{
	acpi_generic_state      *state;

	PROC_NAME ("Ds_result_stack_pop");


	/* Check for stack underflow */

	if (walk_state->results == NULL) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Underflow - State=%p\n",
			walk_state));
		return (AE_AML_NO_OPERAND);
	}


	state = acpi_ut_pop_generic_state (&walk_state->results);

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"Result=%p Remaining_results=%X State=%p\n",
		state, state->results.num_results, walk_state));

	acpi_ut_delete_generic_state (state);

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_obj_stack_delete_all
 *
 * PARAMETERS:  Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear the object stack by deleting all objects that are on it.
 *              Should be used with great care, if at all!
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_delete_all (
	acpi_walk_state         *walk_state)
{
	u32                     i;


	FUNCTION_TRACE_PTR ("Ds_obj_stack_delete_all", walk_state);


	/* The stack size is configurable, but fixed */

	for (i = 0; i < OBJ_NUM_OPERANDS; i++) {
		if (walk_state->operands[i]) {
			acpi_ut_remove_reference (walk_state->operands[i]);
			walk_state->operands[i] = NULL;
		}
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_obj_stack_push
 *
 * PARAMETERS:  Object              - Object to push
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto this walk's object/operand stack
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_push (
	void                    *object,
	acpi_walk_state         *walk_state)
{
	PROC_NAME ("Ds_obj_stack_push");


	/* Check for stack overflow */

	if (walk_state->num_operands >= OBJ_NUM_OPERANDS) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"overflow! Obj=%p State=%p #Ops=%X\n",
			object, walk_state, walk_state->num_operands));
		return (AE_STACK_OVERFLOW);
	}

	/* Put the object onto the stack */

	walk_state->operands [walk_state->num_operands] = object;
	walk_state->num_operands++;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p [%s] State=%p #Ops=%X\n",
			  object, acpi_ut_get_type_name (((acpi_operand_object *) object)->common.type),
			  walk_state, walk_state->num_operands));

	return (AE_OK);
}


#if 0
/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_obj_stack_pop_object
 *
 * PARAMETERS:  Pop_count           - Number of objects/entries to pop
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack.  Objects on the stack are NOT
 *              deleted by this routine.
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_pop_object (
	acpi_operand_object     **object,
	acpi_walk_state         *walk_state)
{
	PROC_NAME ("Ds_obj_stack_pop_object");


	/* Check for stack underflow */

	if (walk_state->num_operands == 0) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Missing operand/stack empty! State=%p #Ops=%X\n",
			walk_state, walk_state->num_operands));
		*object = NULL;
		return (AE_AML_NO_OPERAND);
	}

	/* Pop the stack */

	walk_state->num_operands--;

	/* Check for a valid operand */

	if (!walk_state->operands [walk_state->num_operands]) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Null operand! State=%p #Ops=%X\n",
			walk_state, walk_state->num_operands));
		*object = NULL;
		return (AE_AML_NO_OPERAND);
	}

	/* Get operand and set stack entry to null */

	*object = walk_state->operands [walk_state->num_operands];
	walk_state->operands [walk_state->num_operands] = NULL;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p [%s] State=%p #Ops=%X\n",
			  *object, acpi_ut_get_type_name ((*object)->common.type),
			  walk_state, walk_state->num_operands));

	return (AE_OK);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_obj_stack_pop
 *
 * PARAMETERS:  Pop_count           - Number of objects/entries to pop
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack.  Objects on the stack are NOT
 *              deleted by this routine.
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_pop (
	u32                     pop_count,
	acpi_walk_state         *walk_state)
{
	u32                     i;

	PROC_NAME ("Ds_obj_stack_pop");


	for (i = 0; i < pop_count; i++) {
		/* Check for stack underflow */

		if (walk_state->num_operands == 0) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Underflow! Count=%X State=%p #Ops=%X\n",
				pop_count, walk_state, walk_state->num_operands));
			return (AE_STACK_UNDERFLOW);
		}

		/* Just set the stack entry to null */

		walk_state->num_operands--;
		walk_state->operands [walk_state->num_operands] = NULL;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Count=%X State=%p #Ops=%X\n",
			  pop_count, walk_state, walk_state->num_operands));

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_obj_stack_pop_and_delete
 *
 * PARAMETERS:  Pop_count           - Number of objects/entries to pop
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack and delete each object that is
 *              popped off.
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_pop_and_delete (
	u32                     pop_count,
	acpi_walk_state         *walk_state)
{
	u32                     i;
	acpi_operand_object     *obj_desc;

	PROC_NAME ("Ds_obj_stack_pop_and_delete");


	for (i = 0; i < pop_count; i++) {
		/* Check for stack underflow */

		if (walk_state->num_operands == 0) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Underflow! Count=%X State=%p #Ops=%X\n",
				pop_count, walk_state, walk_state->num_operands));
			return (AE_STACK_UNDERFLOW);
		}

		/* Pop the stack and delete an object if present in this stack entry */

		walk_state->num_operands--;
		obj_desc = walk_state->operands [walk_state->num_operands];
		if (obj_desc) {
			acpi_ut_remove_reference (walk_state->operands [walk_state->num_operands]);
			walk_state->operands [walk_state->num_operands] = NULL;
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Count=%X State=%p #Ops=%X\n",
			  pop_count, walk_state, walk_state->num_operands));

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_obj_stack_get_value
 *
 * PARAMETERS:  Index               - Stack index whose value is desired.  Based
 *                                    on the top of the stack (index=0 == top)
 *              Walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve an object from this walk's object stack.  Index must
 *              be within the range of the current stack pointer.
 *
 ******************************************************************************/

void *
acpi_ds_obj_stack_get_value (
	u32                     index,
	acpi_walk_state         *walk_state)
{

	FUNCTION_TRACE_PTR ("Ds_obj_stack_get_value", walk_state);


	/* Can't do it if the stack is empty */

	if (walk_state->num_operands == 0) {
		return_PTR (NULL);
	}

	/* or if the index is past the top of the stack */

	if (index > (walk_state->num_operands - (u32) 1)) {
		return_PTR (NULL);
	}


	return_PTR (walk_state->operands[(NATIVE_UINT)(walk_state->num_operands - 1) -
			  index]);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_current_walk_state
 *
 * PARAMETERS:  Walk_list       - Get current active state for this walk list
 *
 * RETURN:      Pointer to the current walk state
 *
 * DESCRIPTION: Get the walk state that is at the head of the list (the "current"
 *              walk state.
 *
 ******************************************************************************/

acpi_walk_state *
acpi_ds_get_current_walk_state (
	acpi_walk_list          *walk_list)

{
	PROC_NAME ("Ds_get_current_walk_state");


	ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Ds_get_current_walk_state, =%p\n",
		walk_list->walk_state));

	if (!walk_list) {
		return (NULL);
	}

	return (walk_list->walk_state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_push_walk_state
 *
 * PARAMETERS:  Walk_state      - State to push
 *              Walk_list       - The list that owns the walk stack
 *
 * RETURN:      None
 *
 * DESCRIPTION: Place the Walk_state at the head of the state list.
 *
 ******************************************************************************/

void
acpi_ds_push_walk_state (
	acpi_walk_state         *walk_state,
	acpi_walk_list          *walk_list)
{
	FUNCTION_TRACE ("Ds_push_walk_state");


	walk_state->next    = walk_list->walk_state;
	walk_list->walk_state = walk_state;

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_pop_walk_state
 *
 * PARAMETERS:  Walk_list       - The list that owns the walk stack
 *
 * RETURN:      A Walk_state object popped from the stack
 *
 * DESCRIPTION: Remove and return the walkstate object that is at the head of
 *              the walk stack for the given walk list.  NULL indicates that
 *              the list is empty.
 *
 ******************************************************************************/

acpi_walk_state *
acpi_ds_pop_walk_state (
	acpi_walk_list          *walk_list)
{
	acpi_walk_state         *walk_state;


	FUNCTION_TRACE ("Ds_pop_walk_state");


	walk_state = walk_list->walk_state;

	if (walk_state) {
		/* Next walk state becomes the current walk state */

		walk_list->walk_state = walk_state->next;

		/*
		 * Don't clear the NEXT field, this serves as an indicator
		 * that there is a parent WALK STATE
		 *     Walk_state->Next = NULL;
		 */
	}

	return_PTR (walk_state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_walk_state
 *
 * PARAMETERS:  Origin          - Starting point for this walk
 *              Walk_list       - Owning walk list
 *
 * RETURN:      Pointer to the new walk state.
 *
 * DESCRIPTION: Allocate and initialize a new walk state.  The current walk state
 *              is set to this new state.
 *
 ******************************************************************************/

acpi_walk_state *
acpi_ds_create_walk_state (
	acpi_owner_id           owner_id,
	acpi_parse_object       *origin,
	acpi_operand_object     *mth_desc,
	acpi_walk_list          *walk_list)
{
	acpi_walk_state         *walk_state;
	acpi_status             status;


	FUNCTION_TRACE ("Ds_create_walk_state");


	walk_state = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_WALK);
	if (!walk_state) {
		return_PTR (NULL);
	}

	walk_state->data_type       = ACPI_DESC_TYPE_WALK;
	walk_state->owner_id        = owner_id;
	walk_state->origin          = origin;
	walk_state->method_desc     = mth_desc;
	walk_state->walk_list       = walk_list;

	/* Init the method args/local */

#ifndef _ACPI_ASL_COMPILER
	acpi_ds_method_data_init (walk_state);
#endif

	/* Create an initial result stack entry */

	status = acpi_ds_result_stack_push (walk_state);
	if (ACPI_FAILURE (status)) {
		return_PTR (NULL);
	}

	/* Put the new state at the head of the walk list */

	if (walk_list) {
		acpi_ds_push_walk_state (walk_state, walk_list);
	}

	return_PTR (walk_state);
}


#ifndef _ACPI_ASL_COMPILER
/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_init_aml_walk
 *
 * PARAMETERS:  Walk_state      - New state to be initialized
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a walk state for a pass 1 or 2 parse tree walk
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_aml_walk (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	acpi_namespace_node     *method_node,
	u8                      *aml_start,
	u32                     aml_length,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc,
	u32                     pass_number)
{
	acpi_status             status;
	acpi_parse_state        *parser_state = &walk_state->parser_state;


	FUNCTION_TRACE ("Ds_init_aml_walk");


	walk_state->parser_state.aml    =
	walk_state->parser_state.aml_start = aml_start;
	walk_state->parser_state.aml_end =
	walk_state->parser_state.pkg_end = aml_start + aml_length;

	/* The Next_op of the Next_walk will be the beginning of the method */
	/* TBD: [Restructure] -- obsolete? */

	walk_state->next_op             = NULL;
	walk_state->params              = params;
	walk_state->caller_return_desc  = return_obj_desc;

	status = acpi_ps_init_scope (&walk_state->parser_state, op);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	if (method_node) {
		walk_state->parser_state.start_node = method_node;
		walk_state->walk_type               = WALK_METHOD;
		walk_state->method_node             = method_node;
		walk_state->method_desc             = acpi_ns_get_attached_object (method_node);


		/* Push start scope on scope stack and make it current  */

		status = acpi_ds_scope_stack_push (method_node, ACPI_TYPE_METHOD, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Init the method arguments */

		acpi_ds_method_data_init_args (params, MTH_NUM_ARGS, walk_state);
	}

	else {
		/* Setup the current scope */

		parser_state->start_node = parser_state->start_op->node;
		if (parser_state->start_node) {
			/* Push start scope on scope stack and make it current  */

			status = acpi_ds_scope_stack_push (parser_state->start_node,
					  parser_state->start_node->type, walk_state);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}
	}

	acpi_ds_init_callbacks (walk_state, pass_number);

	return_ACPI_STATUS (AE_OK);
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ds_delete_walk_state
 *
 * PARAMETERS:  Walk_state      - State to delete
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete a walk state including all internal data structures
 *
 ******************************************************************************/

void
acpi_ds_delete_walk_state (
	acpi_walk_state         *walk_state)
{
	acpi_generic_state      *state;


	FUNCTION_TRACE_PTR ("Ds_delete_walk_state", walk_state);


	if (!walk_state) {
		return;
	}

	if (walk_state->data_type != ACPI_DESC_TYPE_WALK) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "%p is not a valid walk state\n", walk_state));
		return;
	}


	if (walk_state->parser_state.scope) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "%p walk still has a scope list\n", walk_state));
	}

   /* Always must free any linked control states */

	while (walk_state->control_state) {
		state = walk_state->control_state;
		walk_state->control_state = state->common.next;

		acpi_ut_delete_generic_state (state);
	}

	/* Always must free any linked parse states */

	while (walk_state->scope_info) {
		state = walk_state->scope_info;
		walk_state->scope_info = state->common.next;

		acpi_ut_delete_generic_state (state);
	}

	/* Always must free any stacked result states */

	while (walk_state->results) {
		state = walk_state->results;
		walk_state->results = state->common.next;

		acpi_ut_delete_generic_state (state);
	}

	acpi_ut_release_to_cache (ACPI_MEM_LIST_WALK, walk_state);
	return_VOID;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ds_delete_walk_state_cache
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Purge the global state object cache.  Used during subsystem
 *              termination.
 *
 ******************************************************************************/

void
acpi_ds_delete_walk_state_cache (
	void)
{
	FUNCTION_TRACE ("Ds_delete_walk_state_cache");


	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_WALK);
	return_VOID;
}


