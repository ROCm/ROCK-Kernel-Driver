/******************************************************************************
 *
 * Module Name: excreate - Named object creation
 *              $Revision: 71 $
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
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acevents.h"
#include "acdispat.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("excreate")


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_alias
 *
 * PARAMETERS:  Walk_state           - Current state, contains List of
 *                                      operands for the opcode
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new named alias
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_alias (
	acpi_walk_state         *walk_state)
{
	acpi_namespace_node     *source_node;
	acpi_status             status;


	FUNCTION_TRACE ("Ex_create_alias");


	/* Get the source/alias operands (both namespace nodes) */

	source_node = (acpi_namespace_node *) walk_state->operands[1];


	/* Attach the original source object to the new Alias Node */

	status = acpi_ns_attach_object ((acpi_namespace_node *) walk_state->operands[0],
			   source_node->object,
			   source_node->type);

	/*
	 * The new alias assumes the type of the source, but it points
	 * to the same object.  The reference count of the object has an
	 * additional reference to prevent deletion out from under either the
	 * source or the alias Node
	 */

	/* Since both operands are Nodes, we don't need to delete them */

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_event
 *
 * PARAMETERS:  Walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new event object
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_event (
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE ("Ex_create_event");


	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_EVENT);
	if (!obj_desc) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Create the actual OS semaphore */

	/* TBD: [Investigate] should be created with 0 or 1 units? */

	status = acpi_os_create_semaphore (ACPI_NO_UNIT_LIMIT, 1,
			   &obj_desc->event.semaphore);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Attach object to the Node */

	status = acpi_ns_attach_object ((acpi_namespace_node *) walk_state->operands[0],
			   obj_desc, (u8) ACPI_TYPE_EVENT);

cleanup:
	/*
	 * Remove local reference to the object (on error, will cause deletion
	 * of both object and semaphore if present.)
	 */
	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_mutex
 *
 * PARAMETERS:  Walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new mutex object
 *
 *              Mutex (Name[0], Sync_level[1])
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_mutex (
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE_PTR ("Ex_create_mutex", WALK_OPERANDS);


	/* Create the new mutex object */

	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_MUTEX);
	if (!obj_desc) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Create the actual OS semaphore */

	status = acpi_os_create_semaphore (1, 1, &obj_desc->mutex.semaphore);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Init object and attach to NS node */

	obj_desc->mutex.sync_level = (u8) walk_state->operands[1]->integer.value;

	status = acpi_ns_attach_object ((acpi_namespace_node *) walk_state->operands[0],
			  obj_desc, (u8) ACPI_TYPE_MUTEX);


cleanup:
	/*
	 * Remove local reference to the object (on error, will cause deletion
	 * of both object and semaphore if present.)
	 */
	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_region
 *
 * PARAMETERS:  Aml_start           - Pointer to the region declaration AML
 *              Aml_length          - Max length of the declaration AML
 *              Operands            - List of operands for the opcode
 *              Walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new operation region object
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_region (
	u8                      *aml_start,
	u32                     aml_length,
	u8                      region_space,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	acpi_operand_object     *obj_desc;
	acpi_namespace_node     *node;


	FUNCTION_TRACE ("Ex_create_region");


	/* Get the Node from the object stack  */

	node = (acpi_namespace_node *) walk_state->operands[0];

	/*
	 * If the region object is already attached to this node,
	 * just return
	 */
	if (node->object) {
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * Space ID must be one of the predefined IDs, or in the user-defined
	 * range
	 */
	if ((region_space >= NUM_REGION_TYPES) &&
		(region_space < USER_REGION_BEGIN)) {
		REPORT_ERROR (("Invalid Address_space type %X\n", region_space));
		return_ACPI_STATUS (AE_AML_INVALID_SPACE_ID);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "Region Type - %s (%X)\n",
			  acpi_ut_get_region_name (region_space), region_space));


	/* Create the region descriptor */

	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_REGION);
	if (!obj_desc) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Allocate a method object for this region */

	obj_desc->region.extra = acpi_ut_create_internal_object (
			 INTERNAL_TYPE_EXTRA);
	if (!obj_desc->region.extra) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/*
	 * Remember location in AML stream of address & length
	 * operands since they need to be evaluated at run time.
	 */
	obj_desc->region.extra->extra.aml_start = aml_start;
	obj_desc->region.extra->extra.aml_length = aml_length;

	/* Init the region from the operands */

	obj_desc->region.space_id = region_space;
	obj_desc->region.address = 0;
	obj_desc->region.length = 0;
	obj_desc->region.node   = node;

	/* Install the new region object in the parent Node */

	status = acpi_ns_attach_object (node, obj_desc,
			  (u8) ACPI_TYPE_REGION);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/*
	 * If we have a valid region, initialize it
	 * Namespace is NOT locked at this point.
	 */
	status = acpi_ev_initialize_region (obj_desc, FALSE);
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

cleanup:

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_table_region
 *
 * PARAMETERS:  Walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new Data_table_region object
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_table_region (
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_create_table_region");

/*
	acpi_operand_object     *Obj_desc;
	Obj_desc = Acpi_ut_create_internal_object (ACPI_TYPE_REGION);
	if (!Obj_desc)
	{
		Status = AE_NO_MEMORY;
		goto Cleanup;
	}


Cleanup:
*/

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_processor
 *
 * PARAMETERS:  Op              - Op containing the Processor definition and
 *                                args
 *              Processor_node  - Parent Node for the processor object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new processor object and populate the fields
 *
 *              Processor (Name[0], Cpu_iD[1], Pblock_addr[2], Pblock_length[3])
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_processor (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	FUNCTION_TRACE_PTR ("Ex_create_processor", walk_state);


	/* Create the processor object */

	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_PROCESSOR);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/*
	 * Initialize the processor object from the operands
	 */
	obj_desc->processor.proc_id = (u8)          operand[1]->integer.value;
	obj_desc->processor.address = (ACPI_IO_ADDRESS) operand[2]->integer.value;
	obj_desc->processor.length = (u8)           operand[3]->integer.value;

	/* Install the processor object in the parent Node */

	status = acpi_ns_attach_object ((acpi_namespace_node *) operand[0],
			  obj_desc, (u8) ACPI_TYPE_PROCESSOR);


	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_power_resource
 *
 * PARAMETERS:  Op              - Op containing the Power_resource definition
 *                                and args
 *              Power_node      - Parent Node for the power object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new Power_resource object and populate the fields
 *
 *              Power_resource (Name[0], System_level[1], Resource_order[2])
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_power_resource (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_status             status;
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE_PTR ("Ex_create_power_resource", walk_state);


	/* Create the power resource object */

	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_POWER);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Initialize the power object from the operands */

	obj_desc->power_resource.system_level = (u8) operand[1]->integer.value;
	obj_desc->power_resource.resource_order = (u16) operand[2]->integer.value;

	/* Install the  power resource object in the parent Node */

	status = acpi_ns_attach_object ((acpi_namespace_node *) operand[0],
			  obj_desc, (u8) ACPI_TYPE_POWER);


	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_create_method
 *
 * PARAMETERS:  Aml_start       - First byte of the method's AML
 *              Aml_length      - AML byte count for this method
 *              Method_flags    - AML method flag byte
 *              Method          - Method Node
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new method object
 *
 ****************************************************************************/

acpi_status
acpi_ex_create_method (
	u8                      *aml_start,
	u32                     aml_length,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *obj_desc;
	acpi_status             status;
	u8                      method_flags;


	FUNCTION_TRACE_PTR ("Ex_create_method", walk_state);


	/* Create a new method object */

	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_METHOD);
	if (!obj_desc) {
	   return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Save the method's AML pointer and length  */

	obj_desc->method.aml_start = aml_start;
	obj_desc->method.aml_length = aml_length;

	/* disassemble the method flags */

	method_flags = (u8) operand[1]->integer.value;

	obj_desc->method.method_flags = method_flags;
	obj_desc->method.param_count = (u8) (method_flags & METHOD_FLAGS_ARG_COUNT);

	/*
	 * Get the concurrency count.  If required, a semaphore will be
	 * created for this method when it is parsed.
	 */
	if (method_flags & METHOD_FLAGS_SERIALIZED) {
		/*
		 * ACPI 1.0: Concurrency = 1
		 * ACPI 2.0: Concurrency = (Sync_level (in method declaration) + 1)
		 */
		obj_desc->method.concurrency = (u8)
				  (((method_flags & METHOD_FLAGS_SYNCH_LEVEL) >> 4) + 1);
	}

	else {
		obj_desc->method.concurrency = INFINITE_CONCURRENCY;
	}

	/* Attach the new object to the method Node */

	status = acpi_ns_attach_object ((acpi_namespace_node *) operand[0],
			  obj_desc, (u8) ACPI_TYPE_METHOD);

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);

	/* Remove a reference to the operand */

	acpi_ut_remove_reference (operand[1]);
	return_ACPI_STATUS (status);
}


